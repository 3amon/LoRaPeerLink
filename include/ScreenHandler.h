#ifndef SCREEN_HANDLER_H_
#define SCREEN_HANDLER_H_

#include "Arduino.h"
#include "WiFi.h"
#include "LoRaWan_APP.h"
#include <Wire.h>  
#include "HT_SSD1306Wire.h"

class ScreenHandler {

    public:
        ScreenHandler();
        void add_line(String str);
        void update_status(String str);
        void setup();

    private:
        String status_line;
        String line1;
        String line2;
        void draw_screen();
};

extern ScreenHandler display_screen;
#endif // T3_LORA