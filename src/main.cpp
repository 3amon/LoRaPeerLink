#include <Arduino.h>
#include <SemtechRadio.h>
#include <LoraBasicLink.h>
#include <ScreenHandler.h>

uint32_t loop_counter = 0;

bool button_pressed = false;

void interrupt_GPIO0();

uint32_t get_time_ms() {
    return millis();
}

void sleep_ms(uint32_t ms) {
    delay(ms);
}

SemtechRadio radio(915000000);
LoRaBasicLink baic_link(&radio, 1, get_time_ms, sleep_ms);

void setup() {
    Serial.begin(115200);
    display_screen.setup();
    attachInterrupt(0, interrupt_GPIO0, FALLING);
    display_screen.update_status("Lora Simp Init");
    
    
}

void loop() {
    radio.begin();

    delay(1000); // Allow radio to initialize
    display_screen.update_status("Lora Simp Ready");

    uint8_t srcId;
    uint8_t buffer[MAX_PAYLOAD];

    while (true)
    {
        if (button_pressed) {
            button_pressed = false;
            display_screen.add_line("Button Pressed");
            baic_link.sendPacket(BROADCAST_ADDR, (uint8_t*)"Button Pressed", 15, false);
            printf("Button Pressed, sending packet\n");
        }

        int rcv_len = baic_link.receivePacket(&srcId, buffer, MAX_PAYLOAD);
        if (rcv_len > 0) {
            display_screen.add_line("SRC: " + String(srcId));
            String message = String((char*)buffer, rcv_len);
            display_screen.add_line("MSG: " + message);
            printf("Received packet from %d: %s\n", srcId, message.c_str());
        }

        delay(1000);
    }
}

void interrupt_GPIO0()
{
    static unsigned long last_interrupt_time = 0;
    unsigned long interrupt_time = millis();
    // If interrupts come faster than 200ms, assume it's a bounce and ignore
    if (interrupt_time - last_interrupt_time > 200) 
    {
        button_pressed = true;
    }
    last_interrupt_time = interrupt_time;
    
}
