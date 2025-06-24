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
        std::unique_ptr<ILoRaLink> link = std::make_unique<LoRaBasicLink>(&radioA, getTimeMock, sleepMock);
        link->setLocalId(1);
        
        uint8_t msg[] = {'t', 'e', 's', 't'};
        REQUIRE(link->sendPacket(1, 2, msg, 4) == true);
        
        // Verify packet was sent to radio
        uint8_t raw[256];
        int len = radioA.receive(raw, 256);
        REQUIRE(len > 0);
    }
    
    SECTION("LoRaBackoffLink through interface") {
        std::unique_ptr<ILoRaLink> link = std::make_unique<LoRaBackoffLink>(&radioB, getTimeMock, sleepMock);
        link->setLocalId(2);
        
        uint8_t msg[] = {'t', 'e', 's', 't'};
        REQUIRE(link->sendPacket(2, 1, msg, 4) == true);
        
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
    links.push_back(std::make_unique<LoRaBasicLink>(&radioA, getTimeMock, sleepMock));
    links.push_back(std::make_unique<LoRaBackoffLink>(&radioB, getTimeMock, sleepMock));
    
    // Set local IDs
    links[0]->setLocalId(1);
    links[1]->setLocalId(2);
    
    // Use both through the same interface
    for (int i = 0; i < links.size(); i++) {
        uint8_t msg[] = {'h', 'i'};
        REQUIRE(links[i]->sendPacket(i+1, 0xFFFF, msg, 2) == true); // Broadcast
    }
}