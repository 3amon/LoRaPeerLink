#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>
#include "ILoRaLink.h"
#include "LoraBasicLink.h"
#include "LoraBackoffLink.h"
#include "TestUtils.h"

// Test that both implementations can be used through the interface
TEST_CASE("ILoRaLink interface works with both implementations", "[ILoRaLink]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();

    SECTION("LoRaBasicLink through interface") {
        std::unique_ptr<ILoRaLink> link = std::make_unique<LoRaBasicLink>(&radioA, 1, getTimeMock, sleepMock);
        
        uint8_t msg[] = {'t', 'e', 's', 't'};
        REQUIRE(link->sendPacket(2, msg, 4) == true);
        
        // Verify packet was sent to radio
        uint8_t raw[256];
        int len = radioA.receive(raw, 256);
        REQUIRE(len > 0);
    }
    
    SECTION("LoRaBackoffLink through interface") {
        std::unique_ptr<ILoRaLink> link = std::make_unique<LoRaBackoffLink>(&radioB, 2, getTimeMock, sleepMock);
        
        uint8_t msg[] = {'t', 'e', 's', 't'};
        REQUIRE(link->sendPacket(1, msg, 4) == true);
        
        // Verify packet was sent to radio  
        uint8_t raw[256];
        int len = radioB.receive(raw, 256);
        REQUIRE(len > 0);
    }
}

// Test polymorphic usage
TEST_CASE("Interface enables polymorphic usage", "[ILoRaLink]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    // Array of different implementations through interface
    std::vector<std::unique_ptr<ILoRaLink>> links;
    links.push_back(std::make_unique<LoRaBasicLink>(&radioA, 1, getTimeMock, sleepMock));
    links.push_back(std::make_unique<LoRaBackoffLink>(&radioB, 2, getTimeMock, sleepMock));
    
    // Use both through the same interface
    for (auto& link : links) {
        uint8_t msg[] = {'h', 'i'};
        REQUIRE(link->sendPacket(255, msg, 2) == true); // Broadcast
    }
}