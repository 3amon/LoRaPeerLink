#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include "LoRaBasicLink.h"
#include "MockRadio.h"

uint32_t mock_time = 0;
uint32_t get_time_ms() { return mock_time; }
void sleep_ms(uint32_t ms) { mock_time += ms; }

TEST_CASE("LoRaBasicLink sends and receives") {
    MockRadio::clearChannel();
    MockRadio r1, r2;
    LoRaBasicLink nodeA(&r1, 0x01, get_time_ms, sleep_ms);
    LoRaBasicLink nodeB(&r2, 0x02, get_time_ms, sleep_ms);

    nodeA.sendPacket(0x02, (uint8_t*)"hello", 5, true);

    uint8_t from, buf[32];
    int len = nodeB.receivePacket(&from, buf, sizeof(buf));

    REQUIRE(len == 5);
    REQUIRE(from == 0x01);
    REQUIRE(std::string((char*)buf, len) == "hello");
}
