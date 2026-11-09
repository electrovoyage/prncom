#include "EspUsbHost.h"
#include <mongoose.h>
#include <WiFi.h>

extern EspUsbHostCdcSerial CdcSerial;
extern RingbufHandle_t toUSB, fromUSB;

#if defined(IFACE_WEBSOCKET) | defined(IFACE_RAW_TCP)
    namespace
    #ifdef IFACE_WEBSOCKET
        WS_COM
    #elifdef IFACE_RAW_TCP
        TCPCOM
    #endif
    {
        extern struct mg_mgr mgr;
    }
#endif

namespace PrnComHttp {
    //#if defined(IFACE_WEBSOCKET) || defined(IFACE_RAW_TCP)
    //    extern struct mg_mgr mgr;
    #ifdef IFACE_WEBSOCKET
        struct mg_mgr& mgr = WS_COM::mgr;
    #elifdef IFACE_RAW_TCP
        struct mg_mgr& mgr = TCPCOM::mgr;
    #else
        #define PRNCOMHTTP_NEEDS_MGR
        struct mg_mgr mgr;
    #endif

    void eventCallback(struct mg_connection* connection, int eventType, void* eventData) {
        switch (eventType) {
            case MG_EV_HTTP_MSG: {
                auto msg = (struct mg_http_message*) eventData;
                if (mg_match(msg->uri, mg_str("/api/status"), NULL)) {
                    UBaseType_t toUsbFree, toUsbWaiting, fromUsbFree, fromUsbWaiting;

                    vRingbufferGetInfo(toUSB, &toUsbFree, NULL, NULL, NULL, &toUsbWaiting);
                    vRingbufferGetInfo(fromUSB, &fromUsbFree, NULL, NULL, NULL, &fromUsbWaiting);

                    mg_http_reply(
                        connection,
                        200,
                        "",
                        "{\n\t%m: %s,\n\t%m: %lu,\n\t%m: %u,\n\t%m: %u,\n\t%m: %u,\n\t%m: %u\n}\n",
                        // It appears everything below it is format arguments.
                        MG_ESC("usb_connected"),
                        CdcSerial.connected() ? "true" : "false",
                        MG_ESC("uptime_ms"),
                        millis(),
                        MG_ESC("to_usb_buffer_free"),
                        toUsbFree,
                        MG_ESC("to_usb_buffer_waiting"),
                        toUsbWaiting,
                        MG_ESC("from_usb_buffer_free"),
                        fromUsbFree,
                        MG_ESC("from_usb_buffer_waiting"),
                        fromUsbWaiting
                    );
                }
                break;
            }
            case MG_EV_CONNECT:
                PRNCOM_LOG("connected");
                break;
            case MG_EV_ERROR:
                PRNCOM_LOGF("\n\n\nERROR: %s (fd %p)\n\n", (char *)eventData, connection->fd);
                break;
        }
    }

    __always_inline void begin() {
        #ifdef PRNCOMHTTP_NEEDS_MGR
            PRNCOM_LOG("connecting to WiFi");
            WiFi.begin(PRNCOM_WIFI_SSID, PRNCOM_WIFI_PASSWORD);
            WiFi.setSleep(WIFI_PS_NONE);

            while (WiFi.status() != WL_CONNECTED);
            PRNCOM_LOGF("WiFi connected at %s", WiFi.localIP().toString().c_str());

            PRNCOM_LOG("initializing Mongoose event manager");
            mg_mgr_init(&mgr);
            mg_log_set(MG_LL_ERROR);
            mg_log_set_fn([](char ch, void *) { Serial.print(ch); }, NULL);
        #endif

        PRNCOM_LOG("Config HTTP server starting.");
        mg_http_listen(&mgr, "http://0.0.0.0:80", eventCallback, NULL);
    }

    __always_inline void loop() {
        #ifdef PRNCOMHTTP_NEEDS_MGR
            mg_mgr_poll(&mgr, 1000);
        #endif
    }
}