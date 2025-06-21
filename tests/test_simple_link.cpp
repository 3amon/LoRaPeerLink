#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include "LoraSimpleLink.h"
#include "MockRadio.h"

uint32_t mock_time = 0;
uint32_t get_time_ms() { return mock_time; }
void sleep_ms(uint32_t ms) { mock_time += ms; }

TEST_CASE("LoRaBasicLink sends and receives") {
    int len;
    MockRadio::clearChannel();
    MockRadio mock;
    mock.begin();
    mock.send(reinterpret_cast<const uint8_t*>("hello"), 5);
    // Now try receiving
    uint8_t buffer[10];
    len = mock.receive(buffer, sizeof(buffer));
    REQUIRE(len == 5);

    MockRadio::clearChannel();
    MockRadio r1, r2;
    LoRaBasicLink nodeA(&r1, 0x01, get_time_ms, sleep_ms);
    LoRaBasicLink nodeB(&r2, 0x02, get_time_ms, sleep_ms);

    nodeA.sendPacket(0x02, (uint8_t*)"hello", 5, true);

    uint8_t from, buf[32];
    len = nodeB.receivePacket(&from, buf, sizeof(buf));

    REQUIRE(len == 5);
    REQUIRE(from == 0x01);
    REQUIRE(std::string((char*)buf, len) == "hello");
}
