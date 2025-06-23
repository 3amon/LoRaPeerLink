#include <catch2/catch_test_macros.hpp>
#include "LoraBasicLink.h"
#include "MockRadio.h"
#include "TestUtils.h"

TEST_CASE("CRC validation in packet transmission", "[CRC]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    
    SECTION("Valid CRC allows packet reception") {
        uint8_t msg[] = {'t', 'e', 's', 't'};
        REQUIRE(linkA.sendPacket(2, msg, 4) == true);
        
        uint8_t src = 0;
        uint8_t out[10];
        int len = linkB.receivePacket(&src, out, 10);
        
        REQUIRE(len == 4);
        REQUIRE(src == 1);
        REQUIRE(out[0] == 't');
        REQUIRE(out[1] == 'e');
        REQUIRE(out[2] == 's');
        REQUIRE(out[3] == 't');
    }
    
    SECTION("Corrupted CRC rejects packet") {
        // Manually create a packet with wrong CRC
        uint8_t corruptPacket[] = {
            2,        // dest
            1,        // src
            0,        // seq
            0,        // flags
            1,        // len
            'x',      // payload
            0x12, 0x34 // wrong CRC
        };
        
        radioA.send(corruptPacket, sizeof(corruptPacket));
        
        uint8_t src = 0;
        uint8_t out[10];
        int len = linkB.receivePacket(&src, out, 10);
        
        REQUIRE(len == 0); // Should be rejected
    }
    
    SECTION("CRC changes with different payloads") {
        // Send two different messages and verify they're both accepted
        // This indirectly tests that CRC calculation is working properly
        
        uint8_t msg1[] = {'h', 'e', 'l', 'l', 'o'};
        uint8_t msg2[] = {'w', 'o', 'r', 'l', 'd'};
        
        REQUIRE(linkA.sendPacket(2, msg1, 5) == true);
        REQUIRE(linkA.sendPacket(2, msg2, 5) == true);
        
        uint8_t src = 0;
        uint8_t out[10];
        
        // First message (first sent, first received)
        int len1 = linkB.receivePacket(&src, out, 10);
        REQUIRE(len1 == 5);
        REQUIRE(memcmp(out, msg1, 5) == 0);
        
        // Second message  
        int len2 = linkB.receivePacket(&src, out, 10);
        REQUIRE(len2 == 5);
        REQUIRE(memcmp(out, msg2, 5) == 0);
    }
    
    SECTION("CRC protects against payload corruption") {
        uint8_t msg[] = {'g', 'o', 'o', 'd'};
        REQUIRE(linkA.sendPacket(2, msg, 4) == true);
        
        // Get the raw packet
        uint8_t buffer[256];
        int packetLen = radioB.receive(buffer, 256);
        REQUIRE(packetLen > 0);
        
        // Corrupt the payload byte
        if (packetLen > 5) {
            buffer[5] = 'X'; // Change first payload byte
        }
        
        // Put corrupted packet back
        MockRadio::clearChannel();
        radioA.send(buffer, packetLen);
        
        uint8_t src = 0;
        uint8_t out[10];
        int len = linkB.receivePacket(&src, out, 10);
        
        REQUIRE(len == 0); // Should be rejected due to CRC mismatch
    }
}

TEST_CASE("CRC edge cases", "[CRC]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    
    SECTION("Maximum size payload CRC validation") {
        uint8_t largeData[249]; // MAX_PAYLOAD size
        for (int i = 0; i < 249; i++) {
            largeData[i] = i & 0xFF;
        }
        
        REQUIRE(linkA.sendPacket(2, largeData, 249) == true);
        
        uint8_t src = 0;
        uint8_t out[249];
        int len = linkB.receivePacket(&src, out, 249);
        REQUIRE(len == 249);
        
        // Verify data integrity
        for (int i = 0; i < 249; i++) {
            REQUIRE(out[i] == (i & 0xFF));
        }
    }
    
    SECTION("Single byte payload CRC validation") {
        uint8_t data[] = {0x55};
        REQUIRE(linkA.sendPacket(2, data, 1) == true);
        
        uint8_t src = 0;
        uint8_t out[10];
        int len = linkB.receivePacket(&src, out, 10);
        REQUIRE(len == 1);
        REQUIRE(out[0] == 0x55);
    }
    
    SECTION("Empty payload CRC validation") {
        REQUIRE(linkA.sendPacket(2, nullptr, 0) == true);
        
        uint8_t src = 0;
        uint8_t out[10];
        int len = linkB.receivePacket(&src, out, 10);
        REQUIRE(len == 0);
        REQUIRE(src == 1); // Source should still be valid
    }
    
    SECTION("All zeros payload") {
        uint8_t zeros[10] = {0};
        REQUIRE(linkA.sendPacket(2, zeros, 10) == true);
        
        uint8_t src = 0;
        uint8_t out[15];
        int len = linkB.receivePacket(&src, out, 15);
        REQUIRE(len == 10);
        
        for (int i = 0; i < 10; i++) {
            REQUIRE(out[i] == 0);
        }
    }
    
    SECTION("All ones payload") {
        uint8_t ones[10];
        memset(ones, 0xFF, 10);
        REQUIRE(linkA.sendPacket(2, ones, 10) == true);
        
        uint8_t src = 0;
        uint8_t out[15];
        int len = linkB.receivePacket(&src, out, 15);
        REQUIRE(len == 10);
        
        for (int i = 0; i < 10; i++) {
            REQUIRE(out[i] == 0xFF);
        }
    }
}