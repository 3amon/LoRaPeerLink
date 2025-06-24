/**
 * @file test_encrypted_link.cpp
 * @brief Tests for EncryptedLoRaLink encryption layer
 * @author LoRaPeerLink Project
 * @version 1.0
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "EncryptedLoRaLink.h"
#include "LoraBasicLink.h"
#include "TestUtils.h"
#include <vector>

TEST_CASE("EncryptedLoRaLink basic functionality", "[EncryptedLoRaLink]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    // Create base links
    LoRaBasicLink baseLinkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink baseLinkB(&radioB, getTimeMock, sleepMock);
    
    // Create encrypted links with same network credentials
    EncryptedLoRaLink encLinkA(&baseLinkA, "TestNetwork", "SecurePassword123");
    EncryptedLoRaLink encLinkB(&baseLinkB, "TestNetwork", "SecurePassword123");
    
    // Set local IDs
    encLinkA.setLocalId(1);
    encLinkB.setLocalId(2);
    
    SECTION("Basic encrypted message transmission") {
        std::string message = "Hello, encrypted world!";
        
        // Send encrypted message
        bool sent = encLinkA.sendPacket(1, 2, 
                                       reinterpret_cast<const uint8_t*>(message.c_str()), 
                                       message.length());
        REQUIRE(sent == true);
        
        // Receive and decrypt message
        uint16_t srcId;
        uint8_t buffer[200];
        int receivedLen = encLinkB.receivePacket(&srcId, buffer, 200);
        
        REQUIRE(receivedLen == message.length());
        REQUIRE(srcId == 1);
        
        // Verify decrypted message matches original
        std::string received(reinterpret_cast<char*>(buffer), receivedLen);
        REQUIRE(received == message);
    }
    
    SECTION("Different network names produce different encryption") {
        // Create another encrypted link with different network name
        EncryptedLoRaLink encLinkC(&baseLinkA, "DifferentNetwork", "SecurePassword123");
        encLinkC.setLocalId(1);
        
        std::string message = "Test message";
        
        // Send from different network
        bool sent = encLinkC.sendPacket(1, 2, 
                                       reinterpret_cast<const uint8_t*>(message.c_str()), 
                                       message.length());
        REQUIRE(sent == true);
        
        // Try to receive with original network credentials
        uint16_t srcId;
        uint8_t buffer[200];
        int receivedLen = encLinkB.receivePacket(&srcId, buffer, 200);
        
        // Should fail to decrypt or return garbage
        if (receivedLen > 0) {
            std::string received(reinterpret_cast<char*>(buffer), receivedLen);
            REQUIRE(received != message); // Should not match original
        }
    }
    
    SECTION("Different passwords produce different encryption") {
        // Create another encrypted link with different password
        EncryptedLoRaLink encLinkD(&baseLinkA, "TestNetwork", "DifferentPassword");
        encLinkD.setLocalId(1);
        
        std::string message = "Test message";
        
        // Send with different password
        bool sent = encLinkD.sendPacket(1, 2, 
                                       reinterpret_cast<const uint8_t*>(message.c_str()), 
                                       message.length());
        REQUIRE(sent == true);
        
        // Try to receive with original password
        uint16_t srcId;
        uint8_t buffer[200];
        int receivedLen = encLinkB.receivePacket(&srcId, buffer, 200);
        
        // Should fail to decrypt or return garbage
        if (receivedLen > 0) {
            std::string received(reinterpret_cast<char*>(buffer), receivedLen);
            REQUIRE(received != message); // Should not match original
        }
    }
}

TEST_CASE("EncryptedLoRaLink header preservation", "[EncryptedLoRaLink]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink baseLinkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink baseLinkB(&radioB, getTimeMock, sleepMock);
    
    EncryptedLoRaLink encLinkA(&baseLinkA, "TestNetwork", "Password123");
    EncryptedLoRaLink encLinkB(&baseLinkB, "TestNetwork", "Password123");
    
    encLinkA.setLocalId(1);
    encLinkB.setLocalId(2);
    
    SECTION("Headers remain unencrypted for routing") {
        std::string message = "Secret message";
        
        // Send encrypted message
        bool sent = encLinkA.sendPacket(1, 2, 
                                       reinterpret_cast<const uint8_t*>(message.c_str()), 
                                       message.length());
        REQUIRE(sent == true);
        
        // Check that raw packet has readable headers
        uint8_t rawPacket[256];
        int rawLen = radioB.receive(rawPacket, sizeof(rawPacket));
        REQUIRE(rawLen > 7); // At least header size
        
        // Verify header fields are readable (not encrypted)
        // Packet format: [dst][src][seq][flags][len][payload][crc]
        uint16_t dst = (rawPacket[0] << 8) | rawPacket[1];   // Big-endian
        uint16_t src = (rawPacket[2] << 8) | rawPacket[3];   // Big-endian
        REQUIRE(dst == 2);  // Destination should be readable
        REQUIRE(src == 1);  // Source should be readable
        
        // Payload should be encrypted (IV + encrypted data)
        // First 16 bytes after header should be IV, rest should be encrypted
        uint8_t payloadStart = 7; // After header
        REQUIRE(rawLen > payloadStart + 16); // Must have at least IV
        
        // The encrypted payload should not match the original message
        std::string payloadPortion(reinterpret_cast<char*>(rawPacket + payloadStart + 16), 
                                  message.length());
        REQUIRE(payloadPortion != message); // Should be encrypted
    }
}

TEST_CASE("EncryptedLoRaLink payload size limits", "[EncryptedLoRaLink]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink baseLinkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink baseLinkB(&radioB, getTimeMock, sleepMock);
    
    EncryptedLoRaLink encLinkA(&baseLinkA, "TestNetwork", "Password123");
    EncryptedLoRaLink encLinkB(&baseLinkB, "TestNetwork", "Password123");
    
    encLinkA.setLocalId(1);
    encLinkB.setLocalId(2);
    
    SECTION("Maximum payload size accounts for encryption overhead") {
        uint8_t maxSize = encLinkA.getMaxPayloadSize();
        REQUIRE(maxSize > 0);
        REQUIRE(maxSize < 200); // Should be less than base link due to IV + padding overhead
    }
    
    SECTION("Large message handling") {
        // Create a message at the maximum size
        uint8_t maxSize = encLinkA.getMaxPayloadSize();
        std::vector<uint8_t> largeMessage(maxSize, 'A');
        
        // Should be able to send max size message
        bool sent = encLinkA.sendPacket(1, 2, largeMessage.data(), largeMessage.size());
        REQUIRE(sent == true);
        
        // Should be able to receive and decrypt
        uint16_t srcId;
        uint8_t buffer[200];
        int receivedLen = encLinkB.receivePacket(&srcId, buffer, 200);
        
        REQUIRE(receivedLen == static_cast<int>(largeMessage.size()));
        REQUIRE(memcmp(buffer, largeMessage.data(), largeMessage.size()) == 0);
    }
    
    SECTION("Oversized message rejection") {
        // Try to send a message larger than max size
        uint8_t maxSize = encLinkA.getMaxPayloadSize();
        std::vector<uint8_t> oversizedMessage(maxSize + 1, 'B');
        
        // Should fail to send
        bool sent = encLinkA.sendPacket(1, 2, oversizedMessage.data(), oversizedMessage.size());
        REQUIRE(sent == false);
    }
}

TEST_CASE("EncryptedLoRaLink acknowledgment handling", "[EncryptedLoRaLink]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink baseLinkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink baseLinkB(&radioB, getTimeMock, sleepMock);
    
    EncryptedLoRaLink encLinkA(&baseLinkA, "TestNetwork", "Password123");
    EncryptedLoRaLink encLinkB(&baseLinkB, "TestNetwork", "Password123");
    
    encLinkA.setLocalId(1);
    encLinkB.setLocalId(2);
    
    SECTION("Encrypted message with ACK request") {
        std::string message = "ACK test message";
        
        // Send with ACK request
        bool sent = encLinkA.sendPacket(1, 2, 
                                       reinterpret_cast<const uint8_t*>(message.c_str()), 
                                       message.length(), 
                                       true); // Request ACK
        
        // Process the message on receiver side to generate ACK
        uint16_t srcId;
        uint8_t buffer[200];
        int receivedLen = encLinkB.receivePacket(&srcId, buffer, 200);
        
        REQUIRE(receivedLen == message.length());
        REQUIRE(sent == true); // Should succeed with ACK
    }
}

TEST_CASE("EncryptedLoRaLink broadcast messages", "[EncryptedLoRaLink]") {
    MockRadio radioA, radioB, radioC;
    MockRadio::clearChannel();
    
    LoRaBasicLink baseLinkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink baseLinkB(&radioB, getTimeMock, sleepMock);
    LoRaBasicLink baseLinkC(&radioC, getTimeMock, sleepMock);
    
    // All nodes on same network
    EncryptedLoRaLink encLinkA(&baseLinkA, "BroadcastNetwork", "SharedPassword");
    EncryptedLoRaLink encLinkB(&baseLinkB, "BroadcastNetwork", "SharedPassword");
    EncryptedLoRaLink encLinkC(&baseLinkC, "BroadcastNetwork", "SharedPassword");
    
    encLinkA.setLocalId(1);
    encLinkB.setLocalId(2);
    encLinkC.setLocalId(3);
    
    SECTION("Encrypted broadcast message") {
        std::string broadcastMsg = "Broadcast to all nodes";
        
        // Send broadcast message
        bool sent = encLinkA.sendPacket(1, 0xFFFF, 
                                       reinterpret_cast<const uint8_t*>(broadcastMsg.c_str()), 
                                       broadcastMsg.length());
        REQUIRE(sent == true);
        
        // Both other nodes should receive and decrypt the message
        uint16_t srcId;
        uint8_t buffer[200];
        
        // Node B receives
        int receivedLenB = encLinkB.receivePacket(&srcId, buffer, 200);
        REQUIRE(receivedLenB == broadcastMsg.length());
        REQUIRE(srcId == 1);
        std::string receivedB(reinterpret_cast<char*>(buffer), receivedLenB);
        REQUIRE(receivedB == broadcastMsg);
        
        // Node C receives 
        int receivedLenC = encLinkC.receivePacket(&srcId, buffer, 200);
        REQUIRE(receivedLenC == broadcastMsg.length());
        REQUIRE(srcId == 1);
        std::string receivedC(reinterpret_cast<char*>(buffer), receivedLenC);
        REQUIRE(receivedC == broadcastMsg);
    }
}

TEST_CASE("EncryptedLoRaLink edge cases", "[EncryptedLoRaLink]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink baseLinkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink baseLinkB(&radioB, getTimeMock, sleepMock);
    
    EncryptedLoRaLink encLinkA(&baseLinkA, "TestNetwork", "Password");
    EncryptedLoRaLink encLinkB(&baseLinkB, "TestNetwork", "Password");
    
    encLinkA.setLocalId(1);
    encLinkB.setLocalId(2);
    
    SECTION("Empty message handling") {
        // Should handle empty messages gracefully
        bool sent = encLinkA.sendPacket(1, 2, nullptr, 0);
        REQUIRE(sent == false); // Should reject empty messages
    }
    
    SECTION("Single byte message") {
        uint8_t singleByte = 0x42;
        
        bool sent = encLinkA.sendPacket(1, 2, &singleByte, 1);
        REQUIRE(sent == true);
        
        uint16_t srcId;
        uint8_t buffer[200];
        int receivedLen = encLinkB.receivePacket(&srcId, buffer, 200);
        
        REQUIRE(receivedLen == 1);
        REQUIRE(buffer[0] == 0x42);
    }
    
    SECTION("Binary data handling") {
        // Test with binary data including null bytes
        uint8_t binaryData[] = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0x00, 0x42};
        size_t dataLen = sizeof(binaryData);
        
        bool sent = encLinkA.sendPacket(1, 2, binaryData, dataLen);
        REQUIRE(sent == true);
        
        uint16_t srcId;
        uint8_t buffer[200];
        int receivedLen = encLinkB.receivePacket(&srcId, buffer, 200);
        
        REQUIRE(receivedLen == static_cast<int>(dataLen));
        REQUIRE(memcmp(buffer, binaryData, dataLen) == 0);
    }
}