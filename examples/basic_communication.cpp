/**
 * @file basic_communication.cpp
 * @brief Basic LoRaPeerLink communication example
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This example demonstrates the simplest possible usage of the LoRaPeerLink library
 * for peer-to-peer LoRa communication. It shows:
 * - Radio initialization
 * - Basic packet transmission and reception
 * - Minimal error handling
 * 
 * This example is platform-agnostic and can be adapted for any microcontroller
 * by implementing the required timing functions and radio hardware interface.
 */

#include "IRadio.h"
#include "LoraBasicLink.h"

// Platform-specific includes would go here
// For Arduino: #include <Arduino.h>
// For other platforms: appropriate timing/delay headers

// Forward declarations of platform-specific functions
uint32_t get_time_ms();
void sleep_ms(uint32_t ms);
void debug_print(const char* message);

// Example MockRadio implementation for testing
// In a real application, use SemtechRadio or your hardware-specific radio class
#include "../tests/MockRadio.h"

/**
 * @brief Simple LoRa communication example
 * 
 * This function demonstrates basic bidirectional communication between two nodes.
 * In a real application, you would typically run this in your main loop.
 */
void basic_communication_example() {
    debug_print("Starting LoRaPeerLink Basic Communication Example");
    
    // 1. Create radio instance
    // For real hardware, use: SemtechRadio radio(915000000);
    MockRadio radio;
    
    // 2. Create basic link layer
    LoRaBasicLink link(&radio, get_time_ms, sleep_ms);
    
    // 3. Initialize the system
    if (!radio.begin()) {
        debug_print("ERROR: Failed to initialize radio");
        return;
    }
    
    // 4. Set node ID (1-65534, 0xFFFF reserved for broadcast)
    link.setLocalId(1);
    
    debug_print("Radio initialized, node ID: 1");
    
    // 5. Main communication loop
    uint32_t last_send_time = 0;
    uint16_t message_counter = 0;
    
    while (true) {
        // Send a message every 5 seconds
        uint32_t current_time = get_time_ms();
        if (current_time - last_send_time > 5000) {
            // Prepare message
            char message[64];
            snprintf(message, sizeof(message), "Hello from Node 1 - Message #%d", message_counter++);
            
            // Send to broadcast address (all nodes will receive)
            bool success = link.sendPacket(1, 0xFFFF, 
                                         reinterpret_cast<const uint8_t*>(message), 
                                         strlen(message), 
                                         false); // no ACK required for broadcast
            
            if (success) {
                debug_print("Message sent successfully");
            } else {
                debug_print("Failed to send message");
            }
            
            last_send_time = current_time;
        }
        
        // Check for incoming messages
        uint16_t source_id;
        uint8_t buffer[MAX_PAYLOAD];
        int received_length = link.receivePacket(&source_id, buffer, MAX_PAYLOAD);
        
        if (received_length > 0) {
            // Null-terminate the received data for printing
            buffer[received_length] = '\0';
            
            char debug_msg[128];
            snprintf(debug_msg, sizeof(debug_msg), 
                    "Received from node %d: %s", source_id, (char*)buffer);
            debug_print(debug_msg);
        }
        
        // Small delay to prevent busy waiting
        sleep_ms(100);
    }
}

/**
 * @brief Example main function
 * 
 * Platform-specific entry point. For Arduino, this would be setup().
 * For other platforms, this might be main() or called from main().
 */
void main_example() {
    // Platform-specific initialization would go here
    // For Arduino: Serial.begin(115200);
    
    // Run the communication example
    basic_communication_example();
}

// Platform-specific implementation examples:

#ifdef ARDUINO
/**
 * @brief Arduino timing function implementation
 */
uint32_t get_time_ms() {
    return millis();
}

/**
 * @brief Arduino delay function implementation  
 */
void sleep_ms(uint32_t ms) {
    delay(ms);
}

/**
 * @brief Arduino debug output implementation
 */
void debug_print(const char* message) {
    Serial.println(message);
}

/**
 * @brief Arduino setup function
 */
void setup() {
    Serial.begin(115200);
    delay(1000); // Allow serial to initialize
    main_example();
}

/**
 * @brief Arduino loop function (not used in this example)
 */
void loop() {
    // Communication loop is handled in basic_communication_example()
    delay(1000);
}

#else
/**
 * @brief Generic/Linux timing function implementation
 */
#include <chrono>
#include <thread>
#include <iostream>

uint32_t get_time_ms() {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void sleep_ms(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void debug_print(const char* message) {
    std::cout << message << std::endl;
}

/**
 * @brief Standard main function for non-Arduino platforms
 */
int main() {
    main_example();
    return 0;
}
#endif