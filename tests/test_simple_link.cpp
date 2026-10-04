#include <thread>
#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include "LoraBasicLink.h"
#include "RollCall.h"
#include "TestUtils.h"


TEST_CASE("LoRaBasicLink sends and receives packets", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);

    uint8_t msg[] = { 'h', 'i' };
    REQUIRE(linkA.sendPacket(1, 2, msg, 2) == true);

    uint16_t src = 0;
    uint8_t out[10];
    int len = linkB.receivePacket(&src, out, 10);
    REQUIRE(len == 2);
    REQUIRE(out[0] == 'h');
    REQUIRE(out[1] == 'i');
    REQUIRE(src == 1);
}

TEST_CASE("LoRaBasicLink ignores packet with invalid CRC", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);

    // Manually corrupt message (updated for 16-bit addressing)
    uint8_t corrupt[] = {0, 2, 0, 1, 0, 0, 1, 'x', 0x12, 0x34}; // wrong CRC
    radioA.send(corrupt, sizeof(corrupt));

    uint16_t src;
    uint8_t out[10];
    int len = linkB.receivePacket(&src, out, 10);
    REQUIRE(len == 0); // Should be rejected
}


TEST_CASE("LoRaBasicLink times out waiting for ACK", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);

    fakeTime = 0;
    REQUIRE(linkA.sendPacket(1, 2, (uint8_t*)"yo", 2, true) == false); // No ACK response
}

TEST_CASE("LoRaBasicLink broadcast messages", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio radioC;

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    LoRaBasicLink linkC(&radioC, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);
    linkC.setLocalId(3);

    SECTION("Broadcast message structure") {
        uint8_t msg[] = {'b', 'c', 'a', 's', 't'};
        REQUIRE(linkA.sendPacket(1, 0xFFFF, msg, 5) == true); // Broadcast

        // Check the packet structure in air
        uint8_t rawPacket[256];
        int rawLen = radioB.receive(rawPacket, 256);
        REQUIRE(rawLen > 0);
        REQUIRE(rawPacket[0] == 0xFF); // Destination high byte should be 0xFF
        REQUIRE(rawPacket[1] == 0xFF); // Destination low byte should be 0xFF
        REQUIRE(rawPacket[2] == 0x00); // Source high byte should be 0x00
        REQUIRE(rawPacket[3] == 0x01); // Source low byte should be 0x01 (linkA's ID)
    }
    
    SECTION("Both receivers can process broadcast individually") {
        // Send broadcast, let B receive it
        uint8_t msg[] = {'b', 'c', 'a', 's', 't'};
        REQUIRE(linkA.sendPacket(1, 0xFFFF, msg, 5) == true);
        
        uint16_t src = 0;
        uint8_t out[10];
        int lenB = linkB.receivePacket(&src, out, 10);
        REQUIRE(lenB == 5);
        REQUIRE(src == 1);
        REQUIRE(memcmp(out, msg, 5) == 0);
        
        // Send another broadcast for C to receive
        MockRadio::clearChannel();
        REQUIRE(linkA.sendPacket(1, 0xFFFF, msg, 5) == true);
        
        int lenC = linkC.receivePacket(&src, out, 10);
        REQUIRE(lenC == 5);
        REQUIRE(src == 1);
        REQUIRE(memcmp(out, msg, 5) == 0);
    }
}

TEST_CASE("LoRaBasicLink maximum payload size", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);

    SECTION("Maximum payload succeeds") {
        // A LoRa frame holds at most 255 bytes: 246 of payload after the 7 byte
        // header and the 2 byte CRC.
        REQUIRE(MAX_PAYLOAD == 246);
        REQUIRE(linkA.maxPayloadSize() == 246);
        uint8_t maxPayload[MAX_PAYLOAD];
        for (int i = 0; i < MAX_PAYLOAD; i++) {
            maxPayload[i] = i & 0xFF;
        }
        
        REQUIRE(linkA.sendPacket(1, 2, maxPayload, MAX_PAYLOAD) == true);
        
        uint16_t src = 0;
        uint8_t out[MAX_PAYLOAD];
        int len = linkB.receivePacket(&src, out, MAX_PAYLOAD);
        REQUIRE(len == MAX_PAYLOAD);
        REQUIRE(src == 1);
        
        for (int i = 0; i < MAX_PAYLOAD; i++) {
            REQUIRE(out[i] == (i & 0xFF));
        }
    }
    
    SECTION("Oversized payload fails") {
        uint8_t oversized[255] = {0};
        REQUIRE(linkA.sendPacket(1, 2, oversized, MAX_PAYLOAD + 1) == false);
        REQUIRE(linkA.sendPacket(1, 2, oversized, 255) == false);
        REQUIRE(radioB.pending() == 0); // Nothing was transmitted
    }

    SECTION("Null payload with a non-zero length fails") {
        REQUIRE(linkA.sendPacket(1, 2, nullptr, 5) == false);
        REQUIRE(radioB.pending() == 0);
    }
}

TEST_CASE("LoRaBasicLink sequence number handling", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);

    SECTION("Sequence numbers increment") {
        uint8_t msg[] = {'1'};
        
        // Send multiple packets and verify different sequence numbers
        REQUIRE(linkA.sendPacket(1, 2, msg, 1) == true);
        msg[0] = '2';
        REQUIRE(linkA.sendPacket(1, 2, msg, 1) == true);
        msg[0] = '3';
        REQUIRE(linkA.sendPacket(1, 2, msg, 1) == true);
        
        uint16_t src = 0;
        uint8_t out[10];
        
        // Receive and verify packets (MockRadio is FIFO - first sent first received)
        int len1 = linkB.receivePacket(&src, out, 10);
        REQUIRE(len1 == 1);
        REQUIRE(out[0] == '1'); // First packet sent, first received
        
        int len2 = linkB.receivePacket(&src, out, 10);
        REQUIRE(len2 == 1);
        REQUIRE(out[0] == '2');
        
        int len3 = linkB.receivePacket(&src, out, 10);
        REQUIRE(len3 == 1);
        REQUIRE(out[0] == '3'); // Last packet sent, last received
    }
}

TEST_CASE("LoRaBasicLink wrong destination filtering", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio radioC;
    MockRadio::clearChannel();

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    LoRaBasicLink linkC(&radioC, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);
    linkC.setLocalId(3);

    uint8_t msg[] = {'p', 'r', 'i', 'v', 'a', 't', 'e'};
    REQUIRE(linkA.sendPacket(1, 2, msg, 7) == true); // Send to B only

    uint16_t src = 0;
    uint8_t out[10];
    
    // B should receive the message
    int lenB = linkB.receivePacket(&src, out, 10);
    REQUIRE(lenB == 7);
    REQUIRE(src == 1);
    
    // C should not receive the message (wrong destination)
    int lenC = linkC.receivePacket(&src, out, 10);
    REQUIRE(lenC == 0);
}

TEST_CASE("LoRaBasicLink buffer overflow protection", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);

    uint8_t msg[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    REQUIRE(linkA.sendPacket(1, 2, msg, 10) == true);

    uint16_t src = 0;
    uint8_t smallBuffer[5]; // Too small for the message
    
    // A payload that does not fit the caller's buffer is dropped. (Version 1
    // copied the first 5 bytes but returned 10, inviting the caller to read
    // past the end of its buffer.)
    memset(smallBuffer, 0xEE, sizeof(smallBuffer));
    int len = linkB.receivePacket(&src, smallBuffer, 5);
    REQUIRE(len == 0);
    REQUIRE(smallBuffer[0] == 0xEE); // Nothing was written
    REQUIRE(linkB.stats().rxDropped == 1);

    // Null arguments are refused rather than dereferenced.
    REQUIRE(linkA.sendPacket(1, 2, msg, 10) == true);
    REQUIRE(linkB.receivePacket(nullptr, smallBuffer, 5) == 0);
    REQUIRE(linkB.receivePacket(&src, nullptr, 5) == 0);
}

TEST_CASE("LoRaBasicLink empty payload handling", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Set local IDs for address filtering
    linkA.setLocalId(1);
    linkB.setLocalId(2);

    REQUIRE(linkA.sendPacket(1, 2, nullptr, 0) == true);

    uint16_t src = 0;
    uint8_t out[10];
    int len = linkB.receivePacket(&src, out, 10);
    REQUIRE(len == 0);
    REQUIRE(src == 1);
}

TEST_CASE("A clock that never advances cannot hang the library", "[LoRaBasicLink][robustness]") {
    // Broken platform hooks: the time never changes and sleeping does nothing.
    auto frozenClock = []() -> uint32_t { return 1000; };
    auto noSleep = [](uint32_t) {};

    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    LoRaBasicLink link(&radioA, frozenClock, noSleep);
    link.setLocalId(1);

    // Waiting for an ACK that never comes returns instead of spinning forever.
    REQUIRE(link.sendPacket(1, 2, (const uint8_t*)"x", 1, true, 3) == false);

    uint16_t src = 0;
    uint8_t buf[16];
    REQUIRE(link.receivePacket(&src, buf, 16, 1000) == 0);

    RollCall rollCall(&link, "frozen", frozenClock, noSleep, []() -> uint16_t { return 77; });
    REQUIRE(rollCall.begin() == true);
    REQUIRE(rollCall.whoIs("nobody", 1000) == 0);
    REQUIRE(rollCall.whereIs(1234, 1000) == "");
    REQUIRE(rollCall.processMessages(1000) == false);
}
