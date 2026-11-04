#ifndef PRNCOM_WSCOM_HPP
#define PRNCOM_WSCOM_HPP

#include <WiFi.h>
#include <ArduinoWebsockets.h>

// This is only needed for VS Code to see the macros
// defined in prncom.ino.
#ifndef PRNCOM_MAIN_INO
#include "prncom.ino"
#endif

extern RingbufHandle_t toUSB;

#define RECONNECT_DELAY_MS 10000

namespace WS_COM {
    websockets::WebsocketsClient client;
    volatile bool needReconnect = false;

    void reconnect() {
        PRNCOM_LOG("connecting to " PRNCOM_SERVER);
        bool result = client.connect(PRNCOM_SERVER);
        PRNCOM_LOGF("connect %s", result ? "success" : "failure");
        PRNCOM_ASSERT(result, "Connect to WebSocket server");
    }

    __always_inline void closeAndScheduleReconnect() {
        client.close();
        needReconnect = true;
    }

    void onWsMessage(websockets::WebsocketsMessage message) {
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
    }

    __always_inline void begin() {
        PRNCOM_LOG("connecting to WiFi");
        WiFi.begin(PRNCOM_SSID, PRNCOM_PASSWORD);
        WiFi.setSleep(WIFI_PS_NONE);

        while (WiFi.status() != WL_CONNECTED);
        PRNCOM_LOGF("WiFi connected at %s", WiFi.localIP().toString().c_str());

        client.onMessage(onWsMessage);
        client.onEvent(onWsEvent);
        

        reconnect();
    }

    __always_inline void loop() {
        client.poll();
    }

    /// @brief Check if a reconnect has been scheduled and perform it if so.
    /// @return Whether a reconnect actually happened.
    __always_inline bool reconnectIfNecessary() {
        if (needReconnect && !client.available()) {
            needReconnect = false;
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            reconnect();
            return true;
        }
        return false;
    }

    __always_inline bool loop2() {
        return reconnectIfNecessary();
    }
}
#endif