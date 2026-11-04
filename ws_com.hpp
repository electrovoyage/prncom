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

    void onWsMessage(websockets::WebsocketsMessage message) {
        //Serial.write(message.c_str(), message.length());
        if(xRingbufferSend(
            toUSB,
            message.c_str(), 
            message.length(), 
            pdMS_TO_TICKS(WS_MESSAGE_RECEIVE_TIMEOUT_MS)
        ) != pdTRUE) {
            PRNCOM_LOG("toUSB buffer full...");
            client.close();
        }
    }

    void reconnect() {
        PRNCOM_LOG("connecting to " PRNCOM_SERVER);
        bool result = client.connect(PRNCOM_SERVER);
        PRNCOM_LOGF("connect %s", result ? "success" : "failure");
        PRNCOM_ASSERT(result, "Connect to WebSocket server");
    }

    void onWsEvent(websockets::WebsocketsEvent event, String data) {
        switch (event) {
            case websockets::WebsocketsEvent::ConnectionClosed:
                PRNCOM_LOGF("connection lost (%s); reconnecting in %ds", data.c_str(), RECONNECT_DELAY_MS / 1000);
                vTaskDelay(RECONNECT_DELAY_MS);
                //reconnect();
                break;
            case websockets::WebsocketsEvent::ConnectionOpened:
                PRNCOM_LOGF("connection established (%s)", data.c_str());
                break;
            case websockets::WebsocketsEvent::GotPing:
                PRNCOM_LOG("got ping; replying with pong");
                client.pong();
                break;
            case websockets::WebsocketsEvent::GotPong:
                PRNCOM_LOG("got pong");
                break;
        }
    }

    __always_inline void sendStep(const char* message, size_t length) {
        client.sendBinary(message, length);
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
}
#endif