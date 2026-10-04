/**
 * @file main.cpp
 * @brief LoRaPeerLink demo for the Heltec WiFi LoRa 32 V3 / Wireless Stick V3
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * Flash this to two or more boards. Each board
 *   - picks an ID, announces itself and discovers the others (RollCall),
 *   - broadcasts a message when the PRG button (GPIO0) is pressed,
 *   - shows received messages on the display and on the serial console.
 *
 * All boards can use the same firmware: when two boards ask for the same
 * name, one of them appends a random suffix.
 */

#include <Arduino.h>
#include <SemtechRadio.h>
#include <LoraBackoffLink.h>
#include <RollCall.h>
#include <PeerMessenger.h>
#include <ScreenHandler.h>
#include <esp_system.h>

// Set by the button interrupt, cleared by the main loop.
volatile bool button_pressed = false;

void IRAM_ATTR interrupt_GPIO0();

// --- Platform hooks for the library ---

uint32_t get_time_ms() {
    return millis();
}

void sleep_ms(uint32_t ms) {
    delay(ms);
}

// The ESP32 hardware random number generator: different on every board and
// every boot, which is what RollCall needs to pick distinct IDs.
uint16_t random_16() {
    return static_cast<uint16_t>(esp_random());
}

// --- The stack: radio -> link -> naming/discovery -> messaging ---

SemtechRadio radio(915000000);
LoRaBackoffLink lora_link(&radio, get_time_ms, sleep_ms);
RollCall roll_call(&lora_link, "lora-node", get_time_ms, sleep_ms, random_16, RollCall::consoleLog);
PeerMessenger messenger(&roll_call, PeerMessenger::consoleLog);

void setup() {
    Serial.begin(115200);

    display_screen.setup();
    display_screen.update_status("LoRa starting");

    attachInterrupt(0, interrupt_GPIO0, FALLING);

    if (!radio.begin()) {
        display_screen.update_status("Radio failed");
        return;
    }

    // Picks an ID, announces this node and listens for about a second.
    if (!roll_call.begin()) {
        display_screen.update_status("RollCall failed");
        return;
    }
    messenger.begin();

    display_screen.update_status(String(roll_call.getNodeName().c_str()));
    display_screen.add_line("ID " + String(roll_call.getNodeId()));
    printf("Node %s, ID %u\n", roll_call.getNodeName().c_str(), roll_call.getNodeId());
}

void loop() {
    if (button_pressed) {
        button_pressed = false;
        display_screen.add_line("Button -> send");
        const bool sent = messenger.broadcastMessage("Button pressed on " + roll_call.getNodeName());
        printf("Broadcast %s\n", sent ? "sent" : "FAILED");
    }

    // One call does everything: it listens for up to a second, handles
    // discovery traffic, sends this node's periodic announcements and queues
    // user messages. Do not add a delay() to this loop; the library only
    // needs the processor while it is inside this call, and the radio keeps
    // receiving in between.
    messenger.processMessages(1000);

    while (messenger.hasMessage()) {
        UserMessage msg = messenger.receiveMessage();
        const String from = msg.srcName.empty() ? String(msg.srcId) : String(msg.srcName.c_str());
        display_screen.add_line(from + ":");
        display_screen.add_line(String(msg.content.c_str()));
        printf("From %s (ID %u, RSSI %d dBm): %s\n", from.c_str(), msg.srcId, radio.packetRssi(), msg.content.c_str());
    }

    // The name can change if another node had the same one.
    static String shown;
    const String current = String(roll_call.getNodeName().c_str());
    if (current != shown) {
        shown = current;
        display_screen.update_status(current);
    }
}

void IRAM_ATTR interrupt_GPIO0() {
    static volatile unsigned long last_interrupt_time = 0;
    const unsigned long now = millis();
    if (now - last_interrupt_time > 200) {      // Debounce
        button_pressed = true;
    }
    last_interrupt_time = now;
}
