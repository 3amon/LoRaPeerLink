/**
 * @file peer_messaging_example.cpp
 * @brief Example demonstrating the PeerMessenger high-level messaging interface
 * @author LoRaPeerLink Project
 * 
 * This example shows how to use the PeerMessenger class for user-friendly
 * peer-to-peer messaging in a LoRa network. It demonstrates:
 * - Node initialization and discovery
 * - Sending messages by node name and ID
 * - Broadcasting messages
 * - Receiving and handling messages
 */

#include "PeerMessenger.h"
#include "LoraBasicLink.h"
#include "TestUtils.h"  // For mock implementations in this example
#include <iostream>
#include <thread>
#include <chrono>

// Example timing and sleep functions (replace with platform-specific implementations)
uint32_t getCurrentTime() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}

void sleepMs(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Example logging function
void consoleLog(const char* message) {
    std::cout << "[" << getCurrentTime() << "ms] " << message << std::endl;
}

int main() {
    std::cout << "=== LoRaPeerLink PeerMessenger Example ===\n" << std::endl;
    
    // Create mock radios for simulation (replace with real radio instances)
    MockRadio radioSensor, radioGateway, radioRelay;
    MockRadio::clearChannel();
    
    // Create link layers
    LoRaBasicLink linkSensor(&radioSensor, getCurrentTime, sleepMs);
    LoRaBasicLink linkGateway(&radioGateway, getCurrentTime, sleepMs);
    LoRaBasicLink linkRelay(&radioRelay, getCurrentTime, sleepMs);
    
    // Create RollCall instances for node discovery
    RollCall rollCallSensor(&linkSensor, "temp-sensor", getCurrentTime, sleepMs, nullptr, consoleLog);
    RollCall rollCallGateway(&linkGateway, "main-gateway", getCurrentTime, sleepMs, nullptr, consoleLog);
    RollCall rollCallRelay(&linkRelay, "relay-node", getCurrentTime, sleepMs, nullptr, consoleLog);
    
    // Create PeerMessenger instances
    PeerMessenger messengerSensor(&rollCallSensor, consoleLog);
    PeerMessenger messengerGateway(&rollCallGateway, consoleLog);
    PeerMessenger messengerRelay(&rollCallRelay, consoleLog);
    
    std::cout << "1. Initializing nodes..." << std::endl;
    
    // Initialize all components
    if (!rollCallSensor.begin() || !messengerSensor.begin()) {
        std::cerr << "Failed to initialize sensor node" << std::endl;
        return 1;
    }
    
    if (!rollCallGateway.begin() || !messengerGateway.begin()) {
        std::cerr << "Failed to initialize gateway node" << std::endl;
        return 1;
    }
    
    if (!rollCallRelay.begin() || !messengerRelay.begin()) {
        std::cerr << "Failed to initialize relay node" << std::endl;
        return 1;
    }
    
    std::cout << "   Sensor ID: " << rollCallSensor.getNodeId() << std::endl;
    std::cout << "   Gateway ID: " << rollCallGateway.getNodeId() << std::endl;
    std::cout << "   Relay ID: " << rollCallRelay.getNodeId() << std::endl;
    
    std::cout << "\n2. Node discovery phase..." << std::endl;
    
    // Simulate node discovery by letting them announce themselves
    // In a real implementation, nodes would discover each other automatically
    for (int i = 0; i < 10; i++) {
        messengerSensor.processMessages(50);
        messengerGateway.processMessages(50);
        messengerRelay.processMessages(50);
        sleepMs(100);
    }
    
    std::cout << "\n3. Sending messages by name..." << std::endl;
    
    // Sensor sends temperature data to gateway by name
    if (messengerSensor.sendMessage("main-gateway", "Temperature: 23.5°C")) {
        std::cout << "   ✓ Sensor sent temperature data to gateway" << std::endl;
    } else {
        std::cout << "   ✗ Failed to send message (gateway not found)" << std::endl;
    }
    
    // Process messages
    messengerGateway.processMessages(100);
    messengerRelay.processMessages(100);
    
    std::cout << "\n4. Sending messages by ID..." << std::endl;
    
    // Gateway sends command to sensor by ID
    uint16_t sensorId = rollCallSensor.getNodeId();
    if (messengerGateway.sendMessage(sensorId, "CMD: Increase sample rate")) {
        std::cout << "   ✓ Gateway sent command to sensor by ID" << std::endl;
    }
    
    // Process messages
    messengerSensor.processMessages(100);
    messengerRelay.processMessages(100);
    
    std::cout << "\n5. Broadcasting messages..." << std::endl;
    
    // Relay broadcasts network status
    if (messengerRelay.broadcastMessage("NETWORK: All nodes online")) {
        std::cout << "   ✓ Relay broadcast network status" << std::endl;
    }
    
    // Process broadcasts
    messengerSensor.processMessages(100);
    messengerGateway.processMessages(100);
    
    std::cout << "\n6. Receiving and handling messages..." << std::endl;
    
    // Check for messages at each node
    std::vector<std::pair<std::string, PeerMessenger*>> nodes = {
        {"Sensor", &messengerSensor},
        {"Gateway", &messengerGateway},
        {"Relay", &messengerRelay}
    };
    
    for (auto& [nodeName, messenger] : nodes) {
        if (messenger->hasMessage()) {
            std::cout << "   " << nodeName << " has " << messenger->getMessageCount() << " message(s):" << std::endl;
            
            while (messenger->hasMessage()) {
                UserMessage msg = messenger->receiveMessage();
                std::string senderInfo = msg.srcName.empty() ? 
                    "ID " + std::to_string(msg.srcId) : 
                    msg.srcName + " (ID " + std::to_string(msg.srcId) + ")";
                
                std::cout << "     From " << senderInfo << ": \"" << msg.content << "\"" << std::endl;
            }
        } else {
            std::cout << "   " << nodeName << " has no messages" << std::endl;
        }
    }
    
    std::cout << "\n7. Continuous operation simulation..." << std::endl;
    
    // Simulate continuous operation for a few seconds
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(3)) {
        // Each node processes messages regularly
        messengerSensor.processMessages(10);
        messengerGateway.processMessages(10);
        messengerRelay.processMessages(10);
        
        // Handle any incoming messages
        for (auto& [nodeName, messenger] : nodes) {
            while (messenger->hasMessage()) {
                UserMessage msg = messenger->receiveMessage();
                std::string senderInfo = msg.srcName.empty() ? 
                    "ID " + std::to_string(msg.srcId) : msg.srcName;
                std::cout << "   [Runtime] " << nodeName << " <- " << senderInfo << ": \"" << msg.content << "\"" << std::endl;
            }
        }
        
        sleepMs(100);
    }
    
    std::cout << "\n=== Example Complete ===\n" << std::endl;
    std::cout << "Key takeaways:" << std::endl;
    std::cout << "• PeerMessenger provides intuitive messaging interface" << std::endl;
    std::cout << "• Automatic name resolution via RollCall integration" << std::endl;
    std::cout << "• Protocol messages filtered from user messages" << std::endl;
    std::cout << "• Support for targeted and broadcast messaging" << std::endl;
    std::cout << "• Regular processMessages() calls handle network maintenance" << std::endl;
    
    return 0;
}