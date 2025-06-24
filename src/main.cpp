/**
 * @file main.cpp
 * @brief Main application entry point for LoRa peer-to-peer communication demo
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements a simple LoRa communication demonstration application
 * that showcases the LoRaPeerLink library capabilities. It provides:
 * - Basic LoRa packet transmission and reception
 * - Button-triggered message broadcasting
 * - OLED display integration for status and message display
 * - Interrupt-based user input handling
 * 
 * The application demonstrates peer-to-peer LoRa communication without
 * the complexity of LoRaWAN, making it suitable for direct device-to-device
 * communication scenarios.
 */

#include <Arduino.h>
#include <SemtechRadio.h>
#include <LoraBasicLink.h>
#include <ScreenHandler.h>

/**
 * @brief Global loop counter for application timing and debugging
 * 
 * Tracks the number of main loop iterations for timing analysis
 * and debugging purposes. Can be used to implement periodic tasks
 * or performance monitoring.
 */
uint32_t loop_counter = 0;

/**
 * @brief Flag indicating button press event occurred
 * 
 * Set by interrupt handler when GPIO0 button is pressed.
 * Used to trigger message transmission in the main loop.
 * Volatile not needed here as it's only set in interrupt context
 * and cleared in main context with proper debouncing.
 */
bool button_pressed = false;

/**
 * @brief Forward declaration of interrupt handler function
 * 
 * Handles GPIO0 button press interrupts with debouncing logic
 * to prevent spurious activations from button bounce.
 */
void interrupt_GPIO0();

/**
 * @brief Get current time in milliseconds for timing functions
 * @return Current time in milliseconds since boot
 * 
 * Provides a timing function compatible with the LoRaPeerLink
 * library interface. Uses Arduino's millis() function for
 * consistent timing across the application.
 */
uint32_t get_time_ms() {
    return millis();
}

/**
 * @brief Sleep/delay function for timing control
 * @param ms Number of milliseconds to delay
 * 
 * Provides a delay function compatible with the LoRaPeerLink
 * library interface. Uses Arduino's delay() function for
 * simple blocking delays.
 */
void sleep_ms(uint32_t ms) {
    delay(ms);
}

// --- Global Objects ---
SemtechRadio radio(915000000);                                    ///< LoRa radio configured for 915 MHz
LoRaBasicLink baic_link(&radio, 1, get_time_ms, sleep_ms);       ///< Basic link layer with node ID 1

/**
 * @brief Arduino setup function - initializes hardware and peripherals
 * 
 * Performs one-time initialization of the LoRa communication system:
 * - Sets up serial communication for debugging
 * - Initializes OLED display system
 * - Configures button interrupt handling
 * - Displays initial status on screen
 * 
 * This function runs once at startup before the main loop begins.
 */
void setup() {
    // Initialize serial communication for debugging
    Serial.begin(115200);
    
    // Initialize display system
    display_screen.setup();
    
    // Set up button interrupt on GPIO0 (falling edge trigger)
    attachInterrupt(0, interrupt_GPIO0, FALLING);
    
    // Display initialization status
    display_screen.update_status("Lora Simp Init");
}

/**
 * @brief Arduino main loop function - handles LoRa communication
 * 
 * Main application loop that handles:
 * - LoRa radio initialization and status updates
 * - Button press event processing and message transmission
 * - Continuous packet reception and display
 * - Status updates on OLED display
 * 
 * The loop runs continuously after setup() completes, providing
 * real-time LoRa communication capabilities with user interaction.
 */
void loop() {
    // Initialize LoRa radio
    radio.begin();

    // Allow time for radio initialization
    delay(1000);
    display_screen.update_status("Lora Simp Ready");

    // Buffers for packet reception
    uint8_t srcId;
    uint8_t buffer[MAX_PAYLOAD];

    // Main communication loop
    while (true)
    {
        // Handle button press events
        if (button_pressed) {
            button_pressed = false;
            
            // Display button press event
            display_screen.add_line("Button Pressed");
            
            // Broadcast button press message to all nodes
            baic_link.sendPacket(BROADCAST_ADDR, (uint8_t*)"Button Pressed", 15, false);
            
            // Debug output to serial console
            printf("Button Pressed, sending packet\n");
        }

        // Check for incoming packets
        int rcv_len = baic_link.receivePacket(&srcId, buffer, MAX_PAYLOAD);
        if (rcv_len > 0) {
            // Display source node ID
            display_screen.add_line("SRC: " + String(srcId));
            
            // Convert received data to string and display
            String message = String((char*)buffer, rcv_len);
            display_screen.add_line("MSG: " + message);
            
            // Debug output to serial console
            printf("Received packet from %d: %s\n", srcId, message.c_str());
        }

        // Main loop delay - controls polling rate
        delay(1000);
    }
}

/**
 * @brief GPIO0 interrupt handler for button press detection
 * 
 * Handles button press interrupts with software debouncing to prevent
 * spurious activations from mechanical button bounce. The handler:
 * - Records the interrupt time for debouncing
 * - Ignores interrupts that occur within 200ms of the previous one
 * - Sets the button_pressed flag for main loop processing
 * 
 * This approach ensures reliable button press detection while avoiding
 * the complexity of hardware debouncing circuits.
 */
void interrupt_GPIO0()
{
    static unsigned long last_interrupt_time = 0;
    unsigned long interrupt_time = millis();
    
    // Software debouncing: ignore rapid successive interrupts
    if (interrupt_time - last_interrupt_time > 200) 
    {
        button_pressed = true;
    }
    
    last_interrupt_time = interrupt_time;
}
