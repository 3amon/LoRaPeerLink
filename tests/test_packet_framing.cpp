#include <catch2/catch_test_macros.hpp>
#include "LoraBasicLink.h"
#include "MockRadio.h"
#include "TestUtils.h"

TEST_CASE("LoRaBasicLink packet framing", "[PacketFraming]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);

    SECTION("Packet format validation") {
        uint8_t msg[] = {'t', 'e', 's', 't'};
        REQUIRE(linkA.sendPacket(1, 2, msg, 4) == true);
        
        // Manually examine the raw packet structure
        uint8_t rawPacket[256];
        int rawLen = radioB.receive(rawPacket, 256);
        REQUIRE(rawLen == 13); // 7 (header) + 4 (payload) + 2 (CRC)
        
        // Validate packet structure with 16-bit addressing (big-endian/network byte order)
        REQUIRE(rawPacket[0] == 0);    // destination high byte
        REQUIRE(rawPacket[1] == 2);    // destination low byte
        REQUIRE(rawPacket[2] == 0);    // source high byte
        REQUIRE(rawPacket[3] == 1);    // source low byte
        REQUIRE(rawPacket[4] == 0);    // sequence (first packet)
        REQUIRE(rawPacket[5] == 0);    // flags (no ACK requested)
        REQUIRE(rawPacket[6] == 4);    // payload length
        REQUIRE(rawPacket[7] == 't');  // payload start
        REQUIRE(rawPacket[8] == 'e');
        REQUIRE(rawPacket[9] == 's');
        REQUIRE(rawPacket[10] == 't');  // payload end
        // rawPacket[9] and [10] are CRC bytes
    }
    
    SECTION("Malformed packet - too short") {
        uint8_t shortPacket[] = {2, 1, 0}; // Only 3 bytes, need at least 7
        radioA.send(shortPacket, 3);
        
        uint16_t src = 0;
        uint8_t out[10];
        int len = linkB.receivePacket(&src, out, 10);
        REQUIRE(len == 0); // Should be rejected
    }
    
    SECTION("Malformed packet - length mismatch") {
        uint8_t packet[] = {2, 1, 0, 0, 5, 'x', 'y', 0x12, 0x34}; // Claims 5 bytes payload but only has 2
        radioA.send(packet, sizeof(packet));
        
        uint16_t src = 0;
        uint8_t out[10];
        int len = linkB.receivePacket(&src, out, 10);
        REQUIRE(len == 0); // Should be rejected
    }
}

TEST_CASE("Packet size validation", "[PacketFraming]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    SECTION("LoRaBasicLink minimum packet size") {
        LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
        LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
        
        // Set local IDs for address filtering
        linkA.setLocalId(1);
        linkB.setLocalId(2);
        
        // Send empty payload
        REQUIRE(linkA.sendPacket(1, 2, nullptr, 0) == true);
        
        uint8_t rawPacket[256];
        int rawLen = radioB.receive(rawPacket, 256);
        REQUIRE(rawLen == 9); // 7 (header) + 0 (payload) + 2 (CRC)
    }
}

TEST_CASE("Flag handling in packets", "[PacketFraming]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    SECTION("LoRaBasicLink flag handling") {
        LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
        LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
        
        // Set local IDs for address filtering
        linkA.setLocalId(1);
        linkB.setLocalId(2);
        
        // Send without ACK request first
        uint8_t msg[] = {'n', 'o', 'a', 'c', 'k'};
        linkA.sendPacket(1, 2, msg, 5, false);
        
        uint8_t rawPacket[256];
        int rawLen = radioB.receive(rawPacket, 256);
        REQUIRE(rawLen > 0);
        
        uint8_t flags = rawPacket[3];
        REQUIRE((flags & 0x02) == 0); // FLAG_ACK_REQUEST should NOT be set
        
        // Clear channel and test with ACK request (but use timeout so it doesn't hang)
        MockRadio::clearChannel();
        fakeTime = 0;
        
        uint8_t msg2[] = {'a', 'c', 'k'};
        bool result = linkA.sendPacket(1, 2, msg2, 3, true); // This will timeout since no ACK
        REQUIRE(result == false); // Should timeout and return false
    }
}

TEST_CASE("Buffer boundary testing", "[PacketFraming]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    SECTION("Receiver buffer too small for packet") {
        LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
        LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
        
        // Set local IDs for address filtering
        linkA.setLocalId(1);
        linkB.setLocalId(2);
        
        uint8_t largePayload[100];
        for (int i = 0; i < 100; i++) {
            largePayload[i] = i;
        }
        
        REQUIRE(linkA.sendPacket(1, 2, largePayload, 100) == true);
        
        uint16_t src = 0;
        uint8_t smallBuffer[10]; // Too small
        int len = linkB.receivePacket(&src, smallBuffer, 10);
        REQUIRE(len == 100); // Returns full payload length, not truncated
        
        // Verify truncated data is correct
        for (int i = 0; i < 10; i++) {
            REQUIRE(smallBuffer[i] == i);
        }
    }
    
    SECTION("Zero-length buffer") {
        LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
        LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
        
        // Set local IDs for address filtering
        linkA.setLocalId(1);
        linkB.setLocalId(2);
        
        uint8_t payload[] = {'t', 'e', 's', 't'};
        REQUIRE(linkA.sendPacket(1, 2, payload, 4) == true);
        
        uint16_t src = 0;
        int len = linkB.receivePacket(&src, nullptr, 0);
        REQUIRE(len == 4); // Should still return payload length
        REQUIRE(src == 1); // Should still set source
    }
}