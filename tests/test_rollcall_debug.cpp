#include <catch2/catch_test_macros.hpp>
#include "RollCall.h"
#include "TestUtils.h"
#include <iostream>

// Debug test to understand what's happening
TEST_CASE("RollCall debug basic communication", "[RollCall][debug]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    
    // Create nodes with fixed random IDs for debugging
    auto fixedRandom1 = []() { return static_cast<uint16_t>(0x1111); };
    auto fixedRandom2 = []() { return static_cast<uint16_t>(0x2222); };
    
    RollCall rollCallA(&linkA, "debug-a", getTimeMock, sleepMock, fixedRandom1);
    RollCall rollCallB(&linkB, "debug-b", getTimeMock, sleepMock, fixedRandom2);
    
    std::cout << "Before begin: A=" << rollCallA.getNodeId() << ", B=" << rollCallB.getNodeId() << std::endl;
    
    // Initialize A
    REQUIRE(rollCallA.begin() == true);
    std::cout << "After A.begin(): A ID=" << rollCallA.getNodeId() << std::endl;
    
    // Let's see what message A actually sent
    std::string expectedMsg = "HELLOIAM|debug-a AT " + std::to_string(rollCallA.getNodeId());
    std::cout << "Expected HELLOIAM message: '" << expectedMsg << "'" << std::endl;
    
    // Try to process with RollCall directly
    bool processed = rollCallB.processMessages(100);
    std::cout << "RollCall processed: " << processed << std::endl;
    
    // Check mappings
    auto bMap = rollCallB.getNameToIdMap();
    std::cout << "B knows about " << bMap.size() << " nodes:" << std::endl;
    for (const auto& pair : bMap) {
        std::cout << "  " << pair.first << " -> " << pair.second << std::endl;
    }
}