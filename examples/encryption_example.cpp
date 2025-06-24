/**
 * @file encryption_example.cpp
 * @brief Example usage of EncryptedLoRaLink encryption layer
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This example demonstrates how to use the EncryptedLoRaLink class to add
 * encryption to your LoRa communication while maintaining compatibility
 * with the existing protocol stack.
 */

#include "EncryptedLoRaLink.h"
#include "LoraBasicLink.h"
#include "RollCall.h"
#include "SemtechRadio.h"
#include <iostream>
#include <string>

// Example timing functions (implementation depends on platform)
uint32_t getTimeMs() {
    // Return current time in milliseconds
    // Implementation varies by platform (Arduino: millis(), Linux: clock_gettime, etc.)
    return 0; // Placeholder
}

void sleepMs(uint32_t ms) {
    // Sleep for specified milliseconds
    // Implementation varies by platform (Arduino: delay(), Linux: usleep, etc.)
}

/**
 * @brief Example: Basic encrypted communication
 */
void basicEncryptedCommunication() {
    std::cout << "=== Basic Encrypted Communication Example ===" << std::endl;
    
    // 1. Initialize radio (example for 915MHz)
    SemtechRadio radio(915000000);  // 915 MHz frequency
    
    // 2. Create base link layer
    LoRaBasicLink baseLink(&radio, getTimeMs, sleepMs);
    
    // 3. Wrap with encryption layer
    EncryptedLoRaLink encryptedLink(&baseLink, "MyNetwork", "SecurePassword123");
    
    // 4. Set local node ID
    encryptedLink.setLocalId(1);
    
    // 5. Send encrypted message
    std::string message = "Hello, secure world!";
    bool success = encryptedLink.sendPacket(
        1,  // source ID
        2,  // destination ID  
        reinterpret_cast<const uint8_t*>(message.c_str()),
        message.length()
    );
    
    if (success) {
        std::cout << "✓ Encrypted message sent successfully" << std::endl;
    } else {
        std::cout << "✗ Failed to send encrypted message" << std::endl;
    }
    
    // 6. Receive encrypted message
    uint16_t sourceId;
    uint8_t buffer[200];
    int receivedLen = encryptedLink.receivePacket(&sourceId, buffer, 200);
    
    if (receivedLen > 0) {
        std::string received(reinterpret_cast<char*>(buffer), receivedLen);
        std::cout << "✓ Received encrypted message: " << received << std::endl;
        std::cout << "  From node: " << sourceId << std::endl;
    }
}

/**
 * @brief Example: Encrypted network with RollCall naming
 */
void encryptedNetworkWithRollCall() {
    std::cout << "\n=== Encrypted Network with RollCall Example ===" << std::endl;
    
    // 1. Initialize radio and base link
    SemtechRadio radio(915000000);
    LoRaBasicLink baseLink(&radio, getTimeMs, sleepMs);
    
    // 2. Add encryption layer
    EncryptedLoRaLink encryptedLink(&baseLink, "SensorNetwork", "NetworkKey2024!");
    
    // 3. Add RollCall naming layer on top of encryption
    RollCall rollCall(&encryptedLink, "sensor-01", getTimeMs, sleepMs);
    
    // 4. Initialize the naming system
    rollCall.begin();
    
    std::cout << "Node initialized as: " << rollCall.getNodeName() 
              << " (ID: " << rollCall.getNodeId() << ")" << std::endl;
    
    // 5. Discover other nodes (encrypted communication)
    std::cout << "Looking for gateway..." << std::endl;
    uint16_t gatewayId = rollCall.whoIs("gateway-main", 5000);
    
    if (gatewayId != 0) {
        std::cout << "✓ Found gateway at ID: " << gatewayId << std::endl;
        
        // 6. Send encrypted data to named node
        std::string sensorData = "temperature:23.5,humidity:65.2";
        bool sent = encryptedLink.sendPacket(
            rollCall.getNodeId(), 
            gatewayId,
            reinterpret_cast<const uint8_t*>(sensorData.c_str()),
            sensorData.length()
        );
        
        if (sent) {
            std::cout << "✓ Encrypted sensor data sent to gateway" << std::endl;
        }
    } else {
        std::cout << "✗ Gateway not found" << std::endl;
    }
}

/**
 * @brief Example: Mixed network (encrypted and unencrypted nodes)
 */
void mixedNetworkExample() {
    std::cout << "\n=== Mixed Network Example ===" << std::endl;
    
    SemtechRadio radio(915000000);
    LoRaBasicLink baseLink(&radio, getTimeMs, sleepMs);
    
    // This node uses encryption
    EncryptedLoRaLink encryptedLink(&baseLink, "SecureZone", "AdminPassword");
    encryptedLink.setLocalId(10);
    
    std::cout << "Secure node initialized (ID: 10)" << std::endl;
    std::cout << "Can communicate with other nodes using 'SecureZone' + 'AdminPassword'" << std::endl;
    std::cout << "Headers remain readable for routing by non-encrypted nodes" << std::endl;
    
    // Headers are still visible for routing, but payload is encrypted
    // Non-encrypted nodes can route but cannot read the payload
    
    // Example: Send to another encrypted node
    std::string secureMessage = "Classified information";
    encryptedLink.sendPacket(10, 11, 
                            reinterpret_cast<const uint8_t*>(secureMessage.c_str()),
                            secureMessage.length());
    
    std::cout << "✓ Secure message sent (only nodes with correct credentials can decrypt)" << std::endl;
}

/**
 * @brief Example: Multiple secure networks
 */
void multipleNetworksExample() {
    std::cout << "\n=== Multiple Secure Networks Example ===" << std::endl;
    
    SemtechRadio radio(915000000);
    LoRaBasicLink baseLink(&radio, getTimeMs, sleepMs);
    
    // Network A: Sensor Network
    EncryptedLoRaLink sensorNetwork(&baseLink, "SensorNet", "SensorPass123");
    sensorNetwork.setLocalId(20);
    
    // Network B: Control Network (different credentials)
    EncryptedLoRaLink controlNetwork(&baseLink, "ControlNet", "ControlPass456");
    controlNetwork.setLocalId(20);  // Same physical node, different logical networks
    
    std::cout << "Node can participate in multiple encrypted networks:" << std::endl;
    std::cout << "- SensorNet: for sensor data communication" << std::endl;
    std::cout << "- ControlNet: for control commands" << std::endl;
    
    // Messages encrypted with different keys based on network
    std::string sensorData = "temp:25.1";
    std::string controlCmd = "pump:on";
    
    sensorNetwork.sendPacket(20, 21, 
                            reinterpret_cast<const uint8_t*>(sensorData.c_str()),
                            sensorData.length());
    
    controlNetwork.sendPacket(20, 22,
                             reinterpret_cast<const uint8_t*>(controlCmd.c_str()),
                             controlCmd.length());
    
    std::cout << "✓ Messages sent on both networks with different encryption" << std::endl;
}

/**
 * @brief Example: Payload size considerations
 */
void payloadSizeExample() {
    std::cout << "\n=== Payload Size Considerations Example ===" << std::endl;
    
    SemtechRadio radio(915000000);
    LoRaBasicLink baseLink(&radio, getTimeMs, sleepMs);
    EncryptedLoRaLink encryptedLink(&baseLink, "TestNet", "TestPass");
    
    // Check maximum payload size
    uint8_t maxSize = encryptedLink.getMaxPayloadSize();
    std::cout << "Maximum encrypted payload size: " << static_cast<int>(maxSize) << " bytes" << std::endl;
    
    // For comparison, base link maximum
    std::cout << "Base link maximum payload: ~247 bytes" << std::endl;
    std::cout << "Encryption overhead: ~" << (247 - maxSize) << " bytes (IV + padding + protocol overhead)" << std::endl;
    
    // Example of payload size planning
    if (maxSize >= 100) {
        std::cout << "✓ Sufficient space for typical sensor data" << std::endl;
    } else {
        std::cout << "⚠ Limited space - consider data compression or chunking" << std::endl;
    }
}

/**
 * @brief Main example function
 */
int main() {
    std::cout << "LoRaPeerLink Encryption Layer Examples\n" << std::endl;
    
    try {
        basicEncryptedCommunication();
        encryptedNetworkWithRollCall();
        mixedNetworkExample();
        multipleNetworksExample();
        payloadSizeExample();
        
        std::cout << "\n=== Examples Complete ===" << std::endl;
        std::cout << "See ENCRYPTION_REPORT.md for detailed technical information" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}