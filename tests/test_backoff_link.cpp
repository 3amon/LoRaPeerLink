#define CATCH_CONFIG_MAIN
#include <thread>
#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include "LoraBackoffLink.h"
#include "TestUtils.h"

TEST_CASE("LoRaBackoffLink sends and receives packets", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, sleepMock);

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

TEST_CASE("LoRaBackoffLink ignores packet with invalid CRC", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, sleepMock);

    // Manually corrupt message
    uint8_t corrupt[] = {2, 1, 0, 0, 1, 'x', 0x12, 0x34}; // wrong CRC
    radioA.send(corrupt, sizeof(corrupt));

    uint8_t src;
    uint8_t out[10];
    int len = linkB.receivePacket(&src, out, 10);
    REQUIRE(len == 0); // Should be rejected
}


TEST_CASE("LoRaBackoffLink handles ACK request with concurrency", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, realSleep);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, realSleep);

    // Background thread to simulate receiver processing
    std::thread receiverThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Wait for message to be sent
        uint8_t src;
        uint8_t buf[32];
        linkB.receivePacket(&src, buf, sizeof(buf));
    });

    fakeTime = 0;
    REQUIRE(linkA.sendPacket(2, (uint8_t*)"ok", 2, true) == true);  // Should get ACK from linkB

    receiverThread.join();
}

TEST_CASE("LoRaBackoffLink times out waiting for ACK", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, sleepMock);

    fakeTime = 0;
    REQUIRE(linkA.sendPacket(2, (uint8_t*)"yo", 2, true) == false); // No ACK response
}

TEST_CASE("LoRaBackoffLink retry mechanism", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, sleepMock);

    SECTION("Successful transmission with retries disabled") {
        uint8_t msg[] = {'o', 'k'};
        REQUIRE(linkA.sendPacket(2, msg, 2, false, 1) == true); // No ACK, single attempt
        
        uint8_t src = 0;
        uint8_t out[10];
        int len = linkB.receivePacket(&src, out, 10);
        REQUIRE(len == 2);
        REQUIRE(src == 1);
        REQUIRE(out[0] == 'o');
        REQUIRE(out[1] == 'k');
    }
    
    SECTION("Multiple retry attempts when ACK required but not received") {
        fakeTime = 0;
        // Request ACK but no one will respond
        REQUIRE(linkA.sendPacket(99, (uint8_t*)"test", 4, true, 2) == false);
        
        // Should have attempted multiple times with backoff delays
        REQUIRE(fakeTime > 100); // Some time should have passed due to backoffs and retries
    }
}

TEST_CASE("LoRaBackoffLink broadcast handling", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio radioC;

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, sleepMock);
    LoRaBackoffLink linkC(&radioC, 3, getTimeMock, sleepMock);

    SECTION("Broadcast message structure") {
        uint8_t msg[] = {'a', 'l', 'l'};
        REQUIRE(linkA.sendPacket(0xFF, msg, 3) == true); // Broadcast

        // Check the packet structure in air
        uint8_t rawPacket[256];
        int rawLen = radioB.receive(rawPacket, 256);
        REQUIRE(rawLen > 0);
        REQUIRE(rawPacket[0] == 0xFF); // Destination should be broadcast address
        REQUIRE(rawPacket[1] == 1);    // Source should be linkA's ID
    }
    
    SECTION("Both receivers can process broadcast individually") {
        // Send broadcast, let B receive it
        uint8_t msg[] = {'a', 'l', 'l'};
        REQUIRE(linkA.sendPacket(0xFF, msg, 3) == true);
        
        uint8_t src = 0;
        uint8_t out[10];
        int lenB = linkB.receivePacket(&src, out, 10);
        REQUIRE(lenB == 3);
        REQUIRE(src == 1);
        REQUIRE(memcmp(out, msg, 3) == 0);
        
        // Send another broadcast for C to receive
        MockRadio::clearChannel();
        REQUIRE(linkA.sendPacket(0xFF, msg, 3) == true);
        
        int lenC = linkC.receivePacket(&src, out, 10);
        REQUIRE(lenC == 3);
        REQUIRE(src == 1);
        REQUIRE(memcmp(out, msg, 3) == 0);
    }
}

TEST_CASE("LoRaBackoffLink maximum payload size", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, sleepMock);

    SECTION("Maximum payload succeeds") {
        uint8_t maxPayload[249]; // BUFFER_SIZE - sizeof(PacketHeader) - 2 = 256 - 5 - 2
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
        uint8_t oversized[250];
        REQUIRE(linkA.sendPacket(2, oversized, 250) == false);
    }
}

TEST_CASE("LoRaBackoffLink backoff timing", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, sleepMock);

    SECTION("Backoff introduces delay") {
        fakeTime = 0;
        uint8_t msg[] = {'t'};
        
        REQUIRE(linkA.sendPacket(2, msg, 1) == true);
        
        // Should introduce some backoff delay
        REQUIRE(fakeTime >= 10); // Minimum initial backoff
    }
    
    SECTION("Different nodes have different backoff patterns") {
        fakeTime = 100; // Start at specific time
        uint32_t startTime = fakeTime;
        
        uint8_t msg[] = {'a'};
        linkA.sendPacket(2, msg, 1);
        uint32_t timeA = fakeTime - startTime;
        
        fakeTime = 100; // Reset to same start time
        linkB.sendPacket(1, msg, 1);
        uint32_t timeB = fakeTime - startTime;
        
        // Times might be different due to random component in backoff
        // But both should have some delay
        REQUIRE(timeA >= 10);
        REQUIRE(timeB >= 10);
    }
}

TEST_CASE("LoRaBackoffLink sequence number uniqueness", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, sleepMock);

    uint8_t msg[] = {'s', 'e', 'q'};
    
    // Send multiple packets
    for (int i = 0; i < 5; i++) {
        msg[2] = '0' + i;
        REQUIRE(linkA.sendPacket(2, msg, 3) == true);
    }
    
    // Verify all packets are received
    uint8_t src = 0;
    uint8_t out[10];
    int received = 0;
    
    while (true) {
        int len = linkB.receivePacket(&src, out, 10);
        if (len == 0) break;
        
        REQUIRE(len == 3);
        REQUIRE(src == 1);
        REQUIRE(out[0] == 's');
        REQUIRE(out[1] == 'e');
        received++;
    }
    
    REQUIRE(received == 5);
}

TEST_CASE("LoRaBackoffLink wrong destination filtering", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio radioC;
    MockRadio::clearChannel();

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, sleepMock);
    LoRaBackoffLink linkC(&radioC, 3, getTimeMock, sleepMock);

    uint8_t msg[] = {'p', 'r', 'i', 'v'};
    REQUIRE(linkA.sendPacket(2, msg, 4) == true); // Send to B only

    uint8_t src = 0;
    uint8_t out[10];
    
    // B should receive
    int lenB = linkB.receivePacket(&src, out, 10);
    REQUIRE(lenB == 4);
    REQUIRE(src == 1);
    
    // C should not receive
    int lenC = linkC.receivePacket(&src, out, 10);
    REQUIRE(lenC == 0);
}

TEST_CASE("LoRaBackoffLink empty payload handling", "[LoRaBackoffLink]") {
    MockRadio radioA;
    MockRadio radioB;
    MockRadio::clearChannel();

    LoRaBackoffLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBackoffLink linkB(&radioB, 2, getTimeMock, sleepMock);

    REQUIRE(linkA.sendPacket(2, nullptr, 0) == true);

    uint8_t src = 0;
    uint8_t out[10];
    int len = linkB.receivePacket(&src, out, 10);
    REQUIRE(len == 0);
    REQUIRE(src == 1);
}
