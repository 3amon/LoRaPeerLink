#include <catch2/catch_test_macros.hpp>
#include "LoraBackoffLink.h"
#include "MockRadio.h"
#include "TestUtils.h"

TEST_CASE("LoRaBackoffLink packet framing", "[BackoffPacketFraming]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBackoffLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, getTimeMock, sleepMock);

    SECTION("Packet header structure") {
        uint8_t msg[] = {'d', 'a', 't', 'a'};
        REQUIRE(linkA.sendPacket(1, 2, msg, 4) == true);
        
        // Manually examine the raw packet
        uint8_t rawPacket[256];
        int rawLen = radioB.receive(rawPacket, 256);
        REQUIRE(rawLen == 13); // 7 (PacketHeader) + 4 (payload) + 2 (CRC)
        
        // Validate PacketHeader structure with 16-bit addressing (little-endian)
        REQUIRE(rawPacket[0] == 2);    // dst low byte
        REQUIRE(rawPacket[1] == 0);    // dst high byte
        REQUIRE(rawPacket[2] == 1);    // src low byte
        REQUIRE(rawPacket[3] == 0);    // src high byte
        REQUIRE(rawPacket[4] == 0);    // seq (first packet)
        REQUIRE(rawPacket[5] == 0);    // flags (no ACK requested)
        REQUIRE(rawPacket[6] == 4);    // len
        REQUIRE(rawPacket[7] == 'd');  // payload
        REQUIRE(rawPacket[8] == 'a');
        REQUIRE(rawPacket[9] == 't');
        REQUIRE(rawPacket[10] == 'a');
    }
    
    SECTION("Malformed packet - insufficient header") {
        uint8_t shortPacket[] = {2, 1}; // Only 2 bytes, need at least 7
        radioA.send(shortPacket, 2);
        
        uint16_t src = 0;
        uint8_t out[10];
        int len = linkB.receivePacket(&src, out, 10);
        REQUIRE(len == 0); // Should be rejected
    }
    
    SECTION("Malformed packet - payload length mismatch") {
        uint8_t packet[] = {2, 1, 0, 0, 10, 'a', 'b', 0x12, 0x34}; // Claims 10 bytes but only has 2
        radioA.send(packet, sizeof(packet));
        
        uint16_t src = 0;
        uint8_t out[10];
        int len = linkB.receivePacket(&src, out, 10);
        REQUIRE(len == 0); // Should be rejected
    }
}

TEST_CASE("LoRaBackoffLink minimum packet size", "[BackoffPacketFraming]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBackoffLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);
    
    // Send empty payload
    REQUIRE(linkA.sendPacket(1, 2, nullptr, 0) == true);
    
    uint8_t rawPacket[256];
    int rawLen = radioB.receive(rawPacket, 256);
    REQUIRE(rawLen == 9); // 7 (PacketHeader) + 0 (payload) + 2 (CRC)
}

TEST_CASE("LoRaBackoffLink flag handling", "[BackoffPacketFraming]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBackoffLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);
    
    // Send without ACK request to test flag is not set
    uint8_t msg[] = {'n', 'o', 'a', 'c', 'k'};
    linkA.sendPacket(1, 2, msg, 5, false);
    
    uint8_t rawPacket[256];
    int rawLen = radioB.receive(rawPacket, 256);
    REQUIRE(rawLen > 0);
    
    uint8_t flags = rawPacket[3];
    REQUIRE((flags & 0x02) == 0); // FLAG_NEEDS_ACK should NOT be set
}