#ifndef PRNCOM_WSCOM_HPP
#define PRNCOM_WSCOM_HPP

#include <WiFi.h>
#include <mongoose.h>
#include <lwip/sockets.h>

// This is only needed for VS Code to see the macros
// defined in prncom.ino.
#ifndef PRNCOM_MAIN_INO
#include "prncom.ino"
#endif

extern RingbufHandle_t toUSB;

#define RECONNECT_DELAY_MS 10000

namespace WS_COM {
    struct mg_mgr mgr;
    volatile bool needReconnect = false;
    volatile bool connected = false;
    struct mg_connection* client;

    __always_inline void scheduleReconnect() {
        needReconnect = true;
    }

    __always_inline void close() {
        // The docs recommend is_draining for closing a connection,
        // though that's for a graceful disconnect. It may be
        // better to return to WEBSOCKET_OP_CLOSE.
        
        // mg_ws_send(client, nullptr, 0, WEBSOCKET_OP_CLOSE);
        client->is_draining = true;
        connected = false;
    }

    __always_inline void closeAndScheduleReconnect() {
        close();
        scheduleReconnect();
    }

    void eventCallback(struct mg_connection* connection, int eventType, void* eventData) {
        switch (eventType) {
            case MG_EV_CONNECT:
                //connection->is_hexdumping = 1;
                PRNCOM_LOG("Connection established");
                break;
            case MG_EV_WS_OPEN:
                PRNCOM_LOG("Websocket connected");
                connected = true;

                const static int one = 1;
                setsockopt((int)connection->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

                break;
            case MG_EV_ERROR:
                //MG_ERROR(("%p %s", connection->fd, (char *)eventData));
                PRNCOM_LOGF("\n\n\nERROR: %s (fd %p)\n\n", (char*) eventData, connection->fd);
                closeAndScheduleReconnect();
                break;
            case MG_EV_WS_MSG:
                {
                    auto message = (struct mg_ws_message*) eventData;
                    //Serial.write(message->data.buf, message->data.len);

                    if (!xRingbufferSend(
                        toUSB,
                        message->data.buf,
                        message->data.len,
                        PRNCOM_WS_MESSAGE_RECEIVE_TIMEOUT_MS
                    )) {
                        PRNCOM_LOG("toUSB buffer full...");
                        closeAndScheduleReconnect();
                    }
                    break;
                }
            case MG_EV_CLOSE:
                PRNCOM_LOG("Websocket closed");
                connected = false;
                scheduleReconnect();
                break;
        }
    }

    void reconnect() {
        PRNCOM_LOG("connecting to " PRNCOM_WS_SERVER);
        
        client = mg_ws_connect(&mgr, PRNCOM_WS_SERVER, eventCallback, nullptr, NULL);
        //while (client && done == false) mg_mgr_poll(&mgr, 1000);
    }

    /*void onWsMessage(websockets::WebsocketsMessage message) {
        //Serial.write(message.c_str(), message.length());
        if(xRingbufferSend(
            toUSB,
            message.c_str(), 
            message.length(), 
            pdMS_TO_TICKS(WS_MESSAGE_RECEIVE_TIMEOUT_MS)
        ) != pdTRUE) {
            PRNCOM_LOG("toUSB buffer full...");
            closeAndScheduleReconnect();
        }
    }

    void onWsEvent(websockets::WebsocketsEvent event, String data) {
        switch (event) {
            case websockets::WebsocketsEvent::ConnectionClosed:
                PRNCOM_LOGF("connection lost (%s); reconnecting in %ds", data.c_str(), RECONNECT_DELAY_MS / 1000);
                vTaskDelay(RECONNECT_DELAY_MS);
                needReconnect = true;
                break;
            case websockets::WebsocketsEvent::ConnectionOpened:
                PRNCOM_LOGF("connection established (%s)", data.c_str());
                break;
            case websockets::WebsocketsEvent::GotPing:
                PRNCOM_LOG("got ping");
                //client.pong();
                break;
            case websockets::WebsocketsEvent::GotPong:
                PRNCOM_LOG("got pong");
                break;
        }
    }

    __always_inline void sendStep(const char* message, size_t length) {
        //client.sendBinary(message, length);
        // WS_MESSAGE_OUTGOING_MAX_CHUNK_SIZE_BYTES
        size_t position = 0;
        while (position < length) {
            size_t remaining = length - position;
            size_t toSend = min(remaining, (size_t)WS_MESSAGE_OUTGOING_MAX_CHUNK_SIZE_BYTES);

            if (!client.sendBinary(message + position, toSend)) {
                PRNCOM_LOG("chunk send failed, closing connection");
                closeAndScheduleReconnect();
                return;
            }

            position += toSend;
            //vTaskDelay(10);
        }
    }*/

    void ledBlinkTask(void* arg) {
        bool lightState = false;
        while (true) {
            lightState = !lightState;
            rgbLedWrite(RGB_BUILTIN, 0, lightState ? 32 : 0, lightState ? 64 : 0);
            vTaskDelay(500);
        }
    }

    /// @brief Check if a reconnect has been scheduled and perform it if so.
    /// @return Whether a reconnect actually happened.
    __always_inline bool reconnectIfNecessary() {
        if (needReconnect) {
            PRNCOM_LOGF("Will reconnect in %ds", RECONNECT_DELAY_MS / 1000);
            needReconnect = false;
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            reconnect();
            return true;
        }
        return false;
    }

    __always_inline void begin() {
        PRNCOM_LOG("connecting to WiFi");
        WiFi.begin(PRNCOM_WIFI_SSID, PRNCOM_WIFI_PASSWORD);
        WiFi.setSleep(WIFI_PS_NONE);

        while (WiFi.status() != WL_CONNECTED);
        PRNCOM_LOGF("WiFi connected at %s", WiFi.localIP().toString().c_str());

        mg_mgr_init(&mgr);
        mg_log_set(MG_LL_ERROR);
        mg_log_set_fn([](char ch, void *) { Serial.print(ch); }, NULL);

        TaskHandle_t blinkTask;
        xTaskCreate(ledBlinkTask, "wsWaitLED", 4096, NULL, 10, &blinkTask);

        reconnect();

        PRNCOM_LOG("awaiting websocket connection");
        while (!connected) {
            mg_mgr_poll(&mgr, 1000);
            reconnectIfNecessary();
        };
        vTaskDelete(blinkTask);
    }

    __always_inline void loop2() {
        mg_mgr_poll(&mgr, 1);
    }

    __always_inline void sendStep(char* item, size_t size) {
        //mg_ws_send(client, item, size, WEBSOCKET_OP_BINARY);
        #ifdef PRNCOM_WS_CHUNKING_ENABLE
            size_t position = 0;
            while (position < size) {
                size_t remaining = size - position;
                size_t toSend = min(remaining, (size_t)PRNCOM_WS_MESSAGE_OUTGOING_MAX_CHUNK_SIZE_BYTES);

                mg_ws_send(client, item + position, toSend, WEBSOCKET_OP_BINARY);
                position += toSend;
                
                // Only wait if it's needed
                if (position < size && client->send.len > 32768) {
                    vTaskDelay(1);
                }
            }
        #else
            mg_ws_send(client, item, size, WEBSOCKET_OP_BINARY);
        #endif
        mg_mgr_poll(&mgr, 0);
    }

    __always_inline bool loop() {
        return reconnectIfNecessary();
    }
}
#endif