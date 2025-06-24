/**
 * @file test_encryption_debug.cpp
 * @brief Debug tests for EncryptedLoRaLink to identify issues
 */

#include <catch2/catch_test_macros.hpp>
#include "EncryptedLoRaLink.h"
#include "LoraBasicLink.h"
#include "TestUtils.h"
#include <iostream>

TEST_CASE("Debug EncryptedLoRaLink", "[Debug]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink baseLinkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink baseLinkB(&radioB, getTimeMock, sleepMock);
    
    EncryptedLoRaLink encLinkA(&baseLinkA, "TestNetwork", "Password123");
    EncryptedLoRaLink encLinkB(&baseLinkB, "TestNetwork", "Password123");
    
    encLinkA.setLocalId(1);
    encLinkB.setLocalId(2);
    
    SECTION("Debug simple message") {
        std::string message = "Hello";
        
        std::cout << "Sending message: " << message << std::endl;
        bool sent = encLinkA.sendPacket(1, 2, 
                                       reinterpret_cast<const uint8_t*>(message.c_str()), 
                                       message.length());
        std::cout << "Send result: " << sent << std::endl;
        
        // Try to receive through encrypted link (don't consume the packet first)
        uint16_t srcId;
        uint8_t buffer[200];
        int receivedLen = encLinkB.receivePacket(&srcId, buffer, 200);
        std::cout << "Received length: " << receivedLen << std::endl;
        
        if (receivedLen > 0) {
            std::string received(reinterpret_cast<char*>(buffer), receivedLen);
            std::cout << "Received message: " << received << std::endl;
        }
        
        REQUIRE(sent == true);
        REQUIRE(receivedLen == message.length());
    }
}