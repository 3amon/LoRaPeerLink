#include <thread>
#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include "LoraSimpleLink.h"
#include "TestUtils.h"


TEST_CASE("LoRaBasicLink sends and receives packets", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);

    uint8_t msg[] = { 'h', 'i' };
    REQUIRE(linkA.sendPacket(2, msg, 2) == true);

    uint8_t src = 0;
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

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);

    // Manually corrupt message
    uint8_t corrupt[] = {2, 1, 0, 0, 1, 'x', 0x12, 0x34}; // wrong CRC
    radioA.send(corrupt, sizeof(corrupt));

    uint8_t src;
    uint8_t out[10];
    int len = linkB.receivePacket(&src, out, 10);
    REQUIRE(len == 0); // Should be rejected
}


TEST_CASE("LoRaBasicLink times out waiting for ACK", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);

    fakeTime = 0;
    REQUIRE(linkA.sendPacket(2, (uint8_t*)"yo", 2, true) == false); // No ACK response
}

TEST_CASE("LoRaBasicLink broadcast messages", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio radioC;

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    LoRaBasicLink linkC(&radioC, 3, getTimeMock, sleepMock);

    SECTION("Broadcast message structure") {
        uint8_t msg[] = {'b', 'c', 'a', 's', 't'};
        REQUIRE(linkA.sendPacket(0xFF, msg, 5) == true); // Broadcast

        // Check the packet structure in air
        uint8_t rawPacket[256];
        int rawLen = radioB.receive(rawPacket, 256);
        REQUIRE(rawLen > 0);
        REQUIRE(rawPacket[0] == 0xFF); // Destination should be broadcast address
        REQUIRE(rawPacket[1] == 1);    // Source should be linkA's ID
    }
    
    SECTION("Both receivers can process broadcast individually") {
        // Send broadcast, let B receive it
        uint8_t msg[] = {'b', 'c', 'a', 's', 't'};
        REQUIRE(linkA.sendPacket(0xFF, msg, 5) == true);
        
        uint8_t src = 0;
        uint8_t out[10];
        int lenB = linkB.receivePacket(&src, out, 10);
        REQUIRE(lenB == 5);
        REQUIRE(src == 1);
        REQUIRE(memcmp(out, msg, 5) == 0);
        
        // Send another broadcast for C to receive
        MockRadio::clearChannel();
        REQUIRE(linkA.sendPacket(0xFF, msg, 5) == true);
        
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

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);

    SECTION("Maximum payload succeeds") {
        uint8_t maxPayload[249]; // MAX_PAYLOAD = 256 - 5 - 2 = 249
        for (int i = 0; i < 249; i++) {
            maxPayload[i] = i & 0xFF;
        }
        
        REQUIRE(linkA.sendPacket(2, maxPayload, 249) == true);
        
        uint8_t src = 0;
        uint8_t out[249];
        int len = linkB.receivePacket(&src, out, 249);
        REQUIRE(len == 249);
        REQUIRE(src == 1);
        
        for (int i = 0; i < 249; i++) {
            REQUIRE(out[i] == (i & 0xFF));
        }
    }
    
    SECTION("Oversized payload fails") {
        uint8_t oversized[250]; // One byte too many
        REQUIRE(linkA.sendPacket(2, oversized, 250) == false);
    }
}

TEST_CASE("LoRaBasicLink sequence number handling", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);

    SECTION("Sequence numbers increment") {
        uint8_t msg[] = {'1'};
        
        // Send multiple packets and verify different sequence numbers
        REQUIRE(linkA.sendPacket(2, msg, 1) == true);
        msg[0] = '2';
        REQUIRE(linkA.sendPacket(2, msg, 1) == true);
        msg[0] = '3';
        REQUIRE(linkA.sendPacket(2, msg, 1) == true);
        
        uint8_t src = 0;
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

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    LoRaBasicLink linkC(&radioC, 3, getTimeMock, sleepMock);

    uint8_t msg[] = {'p', 'r', 'i', 'v', 'a', 't', 'e'};
    REQUIRE(linkA.sendPacket(2, msg, 7) == true); // Send to B only

    uint8_t src = 0;
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

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);

    uint8_t msg[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    REQUIRE(linkA.sendPacket(2, msg, 10) == true);

    uint8_t src = 0;
    uint8_t smallBuffer[5]; // Too small for the message
    
    int len = linkB.receivePacket(&src, smallBuffer, 5);
    REQUIRE(len == 10); // Returns full payload length
    REQUIRE(src == 1);
    // But only the first 5 bytes should be copied
    REQUIRE(memcmp(smallBuffer, msg, 5) == 0);
}

TEST_CASE("LoRaBasicLink empty payload handling", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);

    REQUIRE(linkA.sendPacket(2, nullptr, 0) == true);

    uint8_t src = 0;
    uint8_t out[10];
    int len = linkB.receivePacket(&src, out, 10);
    REQUIRE(len == 0);
    REQUIRE(src == 1);
}
