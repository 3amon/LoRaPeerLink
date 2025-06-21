#define CATCH_CONFIG_MAIN
#include <thread>
#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include "LoraSimpleLink.h"
#include "MockRadio.h"

uint32_t mock_time = 0;
uint32_t get_time_ms() { return mock_time; }
void sleep_ms(uint32_t ms) { mock_time += ms; }

#include <catch2/catch_test_macros.hpp>
#include "LoraSimpleLink.h"
#include "MockRadio.h"

uint32_t fakeTime = 0;
uint32_t getTimeMock() { return fakeTime; }
void sleepMock(uint32_t ms) { fakeTime += ms; }

void realSleep(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

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

TEST_CASE("LoRaBasicLink handles ACK request with concurrency", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, realSleep);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, realSleep);

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

TEST_CASE("LoRaBasicLink times out waiting for ACK", "[LoRaBasicLink]") {
    MockRadio radioA;
    MockRadio radioB;

    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);

    fakeTime = 0;
    REQUIRE(linkA.sendPacket(2, (uint8_t*)"yo", 2, true) == false); // No ACK response
}
