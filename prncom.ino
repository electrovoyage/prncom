#define PRNCOM_MAIN_INO

#include "EspUsbHost.h"

// This is necessary for VS Code to detect xTaskCreatePinnedToCore.
// The real compile can't find this file, though.
#if __has_include(<idf_additions.h>)
    #include <idf_additions.h>
#endif

#define BUFFER_SIZE 10240
#define SEND_THRESHOLD BUFFER_SIZE / 2

#define PRNCOM_LOGF_NOLN(FORMAT, ...) Serial.printf("[%s : %s:%d @ %lums] " FORMAT, __FUNCTION__, __FILE__, __LINE__, millis(), __VA_ARGS__)
#define PRNCOM_LOGF(FORMAT, ...) PRNCOM_LOGF_NOLN(FORMAT "\n", __VA_ARGS__)
#define PRNCOM_LOG(S) PRNCOM_LOGF("%s", S)
#define PRNCOM_LOG_NOLN(S) PRNCOM_LOGF_NOLN("%s", S)

#define PRNCOM_ASSERT(EXPR, COMMENT)                                                                                   \
    if (unlikely(!(EXPR)))                                                                                             \
    {                                                                                                                  \
        PRNCOM_LOGF("assertion failed: %s\n  what failed: %s", #EXPR, COMMENT);                                        \
        Serial.flush();                                                                                                \
        abort();                                                                                                       \
    }

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/task.h"
    #include "freertos/ringbuf.h"
}

RingbufHandle_t toUSB;
RingbufHandle_t fromUSB;

//#define IFACE_HC06
#define IFACE_WEBSOCKET
//#define IFACE_RAW_TCP

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

#define _TOSTRING(X) #X
#define TOSTRING(X) _TOSTRING(X)
#define TOSTRING2(X) TOSTRING(X)
#define PRINT_CONFIG(VALUE) "\n" #VALUE "=" _TOSTRING(VALUE)

#ifdef IFACE_HC06
    #define COM2 HC06

    #define MY_NAME WUSB_TEST
    #define PIN_CODE 7276

    //#define HC06_IMMEDIATE_READ_FEEDBACK
    //#define ENTER_AT_MODE_FIRST

    #include "hc06.hpp"

    #define IMPL_SPECIFIC_CONFIG_OPT PRINT_CONFIG(MY_NAME) PRINT_CONFIG(PIN_CODE)
#elifdef IFACE_WEBSOCKET
    #define COM2 WS_COM

    #define PRNCOM_SSID "purplechatapp"
    #define PRNCOM_PASSWORD "discord.com"
    #define PRNCOM_SERVER "ws://172.24.1.1:7080"
    #define WS_MESSAGE_RECEIVE_TIMEOUT_MS 100
    #define WS_MESSAGE_OUTGOING_MAX_CHUNK_SIZE_BYTES 2048

    #include "ws_com.hpp"

    #define IMPL_SPECIFIC_CONFIG_OPT PRINT_CONFIG(PRNCOM_SSID) PRINT_CONFIG(PRNCOM_PASSWORD) PRINT_CONFIG(PRNCOM_SERVER)
#endif

#define PRNCOM_CONFIGURATION "USB <-> " TOSTRING(COM2) "\n" \
    PRINT_CONFIG(BUFFER_SIZE) PRINT_CONFIG(SEND_THRESHOLD) "\n" \
    IMPL_SPECIFIC_CONFIG_OPT

#define DIVIDER "\n----------------------\n"

void setup() {
    rgbLedWrite(RGB_BUILTIN, 255, 0, 0);
    Serial.begin(115200);
    //delay(5000);

    PRNCOM_LOG("PRNCOM starting. Config:" DIVIDER PRNCOM_CONFIGURATION DIVIDER);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
            PRNCOM_LOG_NOLN("connected: ");
            espUsbHostPrint(device);
            rgbLedWrite(RGB_BUILTIN, 0, 255, 0);
        }
    );

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
            PRNCOM_LOG_NOLN("disconnected: ");
            espUsbHostPrint(device);
            rgbLedWrite(RGB_BUILTIN, 255, 0, 0);
        }
    );

    if (!CdcSerial.setRxBufferSize(8192)) {
        PRNCOM_LOGF("Set Rx buffer size failed: %s", usb.lastErrorName());
        delay(5000);
        ESP.restart();
    }

    if (!CdcSerial.begin(115200)) {
        PRNCOM_LOGF("CDC begin failed: %s", usb.lastErrorName());
        delay(5000);
        ESP.restart();
    }

    if (!usb.begin()) {
        PRNCOM_LOGF("USB begin failed: %s", usb.lastErrorName());
        delay(5000);
        ESP.restart();
    }
    PRNCOM_LOG("USB OK");

    toUSB = xRingbufferCreate(BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    fromUSB = xRingbufferCreate(BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);

    PRNCOM_LOG("init " TOSTRING(COM2));
    COM2::begin();

    PRNCOM_LOG("create communication tasks");

    xTaskCreatePinnedToCore([](void* arg){
        while (true) {
            if (!CdcSerial.connected()) vTaskDelay(1000);

            size_t receivedItemSize;
            auto item = xRingbufferReceive(toUSB, &receivedItemSize, portMAX_DELAY);

            //Serial.write((const char*)item, receivedItemSize);
            size_t size = 0;
            while (size < receivedItemSize) {
                size += CdcSerial.write((char *)(item + size), receivedItemSize - size);
            }
            vRingbufferReturnItem(toUSB, item);
            vTaskDelay(1);
        }
    }, "toCDC", 10240, NULL, configMAX_PRIORITIES - 3, NULL, 1);

    xTaskCreatePinnedToCore([](void* arg){
        while (true) {
            COM2::loop();

            size_t receivedItemSize;
            auto item = xRingbufferReceive(fromUSB, &receivedItemSize, 0);

            if (item != NULL) {
                COM2::sendStep((char*)item, receivedItemSize);
                vRingbufferReturnItem(fromUSB, item);
            }

            COM2::loop2();

            vTaskDelay(1);
        }
    }, "to" TOSTRING(COM2), 10240, NULL, configMAX_PRIORITIES - 3, NULL, 0);

    xTaskCreatePinnedToCore([](void* arg) {
        while (true) {
            if (!CdcSerial.connected()) vTaskDelay(1000);

            auto amount = CdcSerial.available();
            if (amount == 0) {
                vTaskDelay(1);
                continue;
            }

            char* buf = (char*)ps_malloc(amount);
            auto actual_amount = CdcSerial.readBytes(buf, amount);

            // I don't know in what case actual_amount != amount,
            // given that amount comes from available(), but I figured
            // it's a good idea to use that anyway.

            xRingbufferSend(fromUSB, buf, actual_amount, portMAX_DELAY);
            vTaskDelay(1);
        }
    }, "readCDC", 10240, NULL, configMAX_PRIORITIES - 2, NULL, 1);

    PRNCOM_LOG("all done");
}

void loop() {
    //vTaskDelay(portMAX_DELAY);
}