#include <string>

namespace HC06 {
	const static int AT_MODE_BAUDRATE = 9600;
    const static int NORMAL_BAUDRATE = 115200;
    const static int STATE_PIN = 11;
    const static int RX_PIN    = 13;
    const static int TX_PIN    = 12;
    const static int EN_PIN    = 14;

    const static char OK_RESP[]   = "+OK";
    const static char FAIL_RESP[] = "+FAIL";

    bool started = false;
    bool isInAtMode = false;

    /// @brief Prepare pin states.
    void begin() {
        pinMode(STATE_PIN, INPUT);
        pinMode(RX_PIN, INPUT);
        pinMode(TX_PIN, OUTPUT);
        pinMode(EN_PIN, OUTPUT);

        #ifdef ENTER_AT_MODE_FIRST
        HC06::reinit(true);

        vTaskDelay(1000);

        Serial.println("Configure HC06");

        //assert(HC06::at_send_with_confirmation("DEFAULT"));
        PRNCOM_ASSERT(HC06::at_send_with_confirmation("NAME" TOSTRINGVALUE(MY_NAME)), "HC06 set name with confirmation");
        // 8 means 115200
        PRNCOM_ASSERT(HC06::at_send_with_confirmation("BAUD8"), "HC06 set baud rate with confirmation");
        PRNCOM_ASSERT(HC06::at_send_with_confirmation("PIN" TOSTRINGVALUE(PIN_CODE)), "HC06 set PIN with confirmation");
        Serial.println(HC06::at_send_get_response("PIN", 1).at(0).c_str());
        Serial.println(HC06::at_send_get_response("NAME", 1).at(0).c_str());
        Serial.println(HC06::at_send_get_response("BAUD", 1).at(0).c_str());

        Serial.println("Leave AT mode");
        #endif

        #ifdef IFACE_HC06
        HC06::reinit(false);
        #endif
    }

    __always_inline void at_println(const char* msg) {
        Serial1.printf("AT+%s\r\n", msg);
    }

    /// @brief Change HC06 mode.
    /// @param at_mode `bool`: whether AT-mode or normal baud rate should be used. Also enables the EN pin.
    void reinit(bool at_mode) {
		if (started) {
            Serial1.end();
        }
        digitalWrite(EN_PIN, at_mode);

        if (!at_mode && isInAtMode) {
            // We are leaving AT mode. Send a software reset
            at_println("RESET");
        }

        Serial1.begin(at_mode ? AT_MODE_BAUDRATE : NORMAL_BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);
        Serial1.setRxBufferSize(8192);
        Serial1.setTxBufferSize(8192);

        started = true;

        isInAtMode = at_mode;
    }

    /// @brief Internal: read AT-mode string until CRLF.
    /// @note This will block the caller function until a full line can be read
    /// @return `std::string`: read contents without CRLF
    std::string at_receive_line_blocking() {
        std::string s;

		while (true) {
			if (!Serial1.available()) {
				// Prevent setting off the watchdog
                vTaskDelay(0);
                continue;
            };

            char c = (char)Serial1.read();
			#ifdef HC06_IMMEDIATE_READ_FEEDBACK
            if (c != '\r' && c != '\n') {
                Serial.print(c);
            } else {
                Serial.print(c == '\n' ? "\\n" : "\\r");
            }
            #endif

            if (c == '\r') {
                // eat the \n
                while (!Serial1.available());
                Serial1.read();

                break;
            }

            s += c;
        }

        return s;
    }

    /// @brief Internal: send AT-mode message and return response.
    /// @param msg `const char*`: Message to send, without trailing newline or `AT+` prefix.
    /// @param lines `size_t`: Amount of lines to receive from module, default 1.
    /// @return `std::vector<std::string>`: Lines of response, each without newlines.
    __always_inline std::vector<std::string> at_send_get_response(const char* msg, size_t lines = 1) {
        at_println(msg);

        std::vector<std::string> strings;

        for (size_t i = 0; i < lines; i++) {
			std::string s = at_receive_line_blocking();
            String s2 = s.c_str();
            s2.trim();

            std::string final_string(s2.c_str());

            strings.push_back(final_string);
        }

        return strings;
    }

	/// @brief Send AT-mode message and await OK-CRLF confirmation.
	/// @param msg `const char*`: Message to send, without trailing newline or `AT+` prefix.
	/// @param return_value `std::string*`: optional pointer to a string in which to store the actual response.
	/// @return `bool`: `true` if response was OK, `false` otherwise.
	bool at_send_with_confirmation(const char* msg, std::string* return_value = nullptr) {
        std::string response = at_send_get_response(msg, 1).at(0);

        String response2 = response.c_str();
        response2.replace("\r", "\\r");
        response2.replace("\n", "\\n");
        Serial.printf("at_send_with_confirmation received %s\n", response2.c_str());

        if (return_value != nullptr) {
            *return_value = response;
        }

        return response == OK_RESP;
    }

    /// @brief Internal: ask HC06 about property.
    /// @param property `const char*`: name of property to get, without `AT+`, `?` or newlines.
	/// @param lines `size_t`: amount of response lines to read.
    /// @return `std::vector<std::string>` Returned lines.
    __always_inline std::vector<std::string> at_inquire(const char* property, size_t lines) {
        String property_s = property;
        // JDY-31 HC06 does not seem to need the question mark;
        // some commands don't even read with it present.
        // property_s += "?";

        return at_send_get_response(property_s.c_str(), lines);
    }

    /// @brief Get HC06 property and check for OK response.
    /// @param property `const char*`: name of property to get, without `AT+`, `?` or newlines.
    /// @param return_value `std::string*`: string to put response value in, if successful.
    /// @return `bool`: whether fetching property was successful.
    bool at_inquire_with_confirmation(const char* property, std::string* return_value) {
        std::string resp = at_inquire(property, 1).at(0);

		// FAIL may only be the first line.
		if (resp == FAIL_RESP) return false;

        // If the first line wasn't FAIL then no other one can be;
		// there is no AT+? command that returns more than one
		// line of response data.
        *return_value = resp;

		// Finally, eat the OK.
        at_receive_line_blocking();

        return true;
    }

    /// @brief This may be due to a bug in `at_inquire_with_confirmation`, but my HC05 has no newline between the
    /// response and status code. Get HC05 property and check for OK response; truncate string to drop status code.
    /// @param property `const char*`: name of property to get, without `AT+`, `?` or newlines.
    /// @param return_value `std::string*`: string to put response value in, if successful.
    /// @return `bool`: whether fetching property was successful.
    bool at_inquire_with_inline_confirmation(const char* property, std::string* return_value) {
		std::string resp = at_inquire(property, 1).at(0);

		// If failed, "FAIL" is the entire string minus newlines, so return immediately.
		if (resp == FAIL_RESP) return false;

		// If it's not all "FAIL", we have success. That means we need to remove the
		// trailing " OK", the length of which is 3 characters.
        *return_value = resp.substr(0, resp.length() - 3);

        return true;
    }

	__always_inline void print(const char* msg, size_t size) {
        for (size_t i = 0; i < size; i++) {
            Serial1.print(msg[i]);
        }
    }

    #error IMPLEMENT HC06::sendStep!!!
    __always_inline void sendStep(const char* message, size_t length);

    __always_inline void loop() {};
}