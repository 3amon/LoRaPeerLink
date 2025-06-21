#ifndef T3_LORA
#define T3_LORA

#include "Arduino.h"
#include "WiFi.h"
#include <Wire.h>  
#include "HT_SSD1306Wire.h"
#include <map>
#include <SHA256.h>  // Include Arduino SHA256 library
#include <Base64.h>  // Include Arduino Base64 library
#include <ScreenHandler.h>
#include <AES.h>
#include <Base64.h>
#include <AesHandler.hpp>

class LoraHandler {

    public:
        LoraHandler();
        void setup();
        void loop();
        bool newMessageReceived();
        void send_ping();
        void send_msg(String msg);
        String last_message_rcv;
        String current_send_msg;

    private:
        std::map<int, String> receivedChunks;
        String last_rcv_checksum;
        bool new_msg_rcv;
        String ping_msg;
        String chip_id;

        String calculateSHA256(const String& data);
        String base64Decode(const String& encoded);
        String base64Encode(const String& message);
        void receiveMessage(const String& message);
        void processMessage(const String& message);
        bool verifyFileIntegrity();
};



#endif // T3_LORA