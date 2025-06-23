#include <catch2/catch_test_macros.hpp>
#include "RollCall.h"
#include "TestUtils.h"

// Simple deterministic random number generator for testing
class TestRandom {
public:
    TestRandom(uint16_t seed = 12345) : _seed(seed) {}
    
    uint16_t next() {
        _seed = (_seed * 1103515245 + 12345) & 0x7FFFFFFF;
        uint16_t result = (_seed >> 16) & 0xFFFF;
        // Avoid reserved values
        if (result == 0 || result == 0xFFFF) {
            return next();
        }
        return result;
    }
    
private:
    uint32_t _seed;
};

static TestRandom testRng1(12345);
static TestRandom testRng2(54321);

static uint16_t getTestRandom1() {
    return testRng1.next();
}

static uint16_t getTestRandom2() {
    return testRng2.next();
}

TEST_CASE("RollCall basic initialization", "[RollCall]") {
    MockRadio radioA;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    RollCall rollCallA(&linkA, "node-a", getTimeMock, sleepMock, getTestRandom1);
    
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallA.getNodeName() == "node-a");
    REQUIRE(rollCallA.getNodeId() != 0);
    REQUIRE(rollCallA.getNodeId() != 0xFFFF);
    
    // Check that the node knows about itself
    auto nameToId = rollCallA.getNameToIdMap();
    auto idToName = rollCallA.getIdToNameMap();
    
    REQUIRE(nameToId.count("node-a") == 1);
    REQUIRE(nameToId["node-a"] == rollCallA.getNodeId());
    REQUIRE(idToName.count(rollCallA.getNodeId()) == 1);
    REQUIRE(idToName[rollCallA.getNodeId()] == "node-a");
}

TEST_CASE("RollCall HELLOIAM broadcast and reception", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "node-a", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "node-b", getTimeMock, sleepMock, getTestRandom2);
    
    // Initialize A first - this puts A's HELLOIAM message in the air
    REQUIRE(rollCallA.begin() == true);
    
    // B processes A's message before starting itself
    rollCallB.processMessages(100);
    
    // Now initialize B - this puts B's HELLOIAM message in the air
    REQUIRE(rollCallB.begin() == true);
    
    // A processes B's message
    rollCallA.processMessages(100);
    
    // Check that they learned about each other
    auto aNameToId = rollCallA.getNameToIdMap();
    auto bNameToId = rollCallB.getNameToIdMap();
    
    REQUIRE(aNameToId.count("node-b") == 1);
    REQUIRE(aNameToId["node-b"] == rollCallB.getNodeId());
    
    REQUIRE(bNameToId.count("node-a") == 1);
    REQUIRE(bNameToId["node-a"] == rollCallA.getNodeId());
}

TEST_CASE("RollCall WHOIS query", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "sensor-1", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "gateway-1", getTimeMock, sleepMock, getTestRandom2);
    
    // Initialize nodes and let them learn about each other
    REQUIRE(rollCallA.begin() == true);
    rollCallB.processMessages(100);  // B learns about A
    REQUIRE(rollCallB.begin() == true);
    rollCallA.processMessages(100);  // A learns about B
    
    // Clear the channel
    MockRadio::clearChannel();
    
    // Test WHOIS query from A to find B
    std::string queryName = "gateway-1";
    
    // Start the query (this broadcasts the WHOIS message)
    uint16_t foundId = 0;
    
    // Since we can't do this in parallel with the mock, we'll simulate the exchange
    // A sends WHOIS query
    std::string query = std::string("WHOIS|") + queryName;
    REQUIRE(linkA.sendPacket(BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(query.c_str()), 
                            query.length()) == true);
    
    // B receives and processes the query, sends response
    REQUIRE(rollCallB.processMessages(100) == true);
    
    // A receives and processes the response
    REQUIRE(rollCallA.processMessages(100) == true);
    
    // Now A should know about gateway-1
    foundId = rollCallA.whoIs(queryName, 10); // Should be in cache now
    REQUIRE(foundId == rollCallB.getNodeId());
}

TEST_CASE("RollCall WHEREIS query", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "sensor-2", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "controller-1", getTimeMock, sleepMock, getTestRandom2);
    
    // Initialize nodes and let them learn about each other
    REQUIRE(rollCallA.begin() == true);
    rollCallB.processMessages(100);  // B learns about A
    REQUIRE(rollCallB.begin() == true);
    rollCallA.processMessages(100);  // A learns about B
    
    // Clear the channel
    MockRadio::clearChannel();
    
    // Test WHEREIS query from A to find B's name
    uint16_t queryId = rollCallB.getNodeId();
    
    // A sends WHEREIS query
    std::string query = std::string("WHEREIS|") + std::to_string(queryId);
    REQUIRE(linkA.sendPacket(BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(query.c_str()), 
                            query.length()) == true);
    
    // B receives and processes the query, sends response
    REQUIRE(rollCallB.processMessages(100) == true);
    
    // A receives and processes the response
    REQUIRE(rollCallA.processMessages(100) == true);
    
    // Now A should know the name for this ID
    std::string foundName = rollCallA.whereIs(queryId, 10); // Should be in cache now
    REQUIRE(foundName == "controller-1");
}

TEST_CASE("RollCall collision detection and resolution", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    
    // Use a custom random function that returns the same ID initially
    uint16_t fixedId = 0x1234;
    auto sameIdRandom = []() { return static_cast<uint16_t>(0x1234); };
    
    RollCall rollCallA(&linkA, "node-alpha", getTimeMock, sleepMock, sameIdRandom);
    RollCall rollCallB(&linkB, "node-beta", getTimeMock, sleepMock, getTestRandom2);
    
    // Initialize A first
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallA.getNodeId() == fixedId);
    
    // Now initialize B - it should detect collision during begin()
    REQUIRE(rollCallB.begin() == true);
    
    // Let them process messages during the collision backoff period
    for (int i = 0; i < 10; i++) {
        rollCallA.processMessages(50);
        rollCallB.processMessages(50);
    }
    
    // They should have different IDs now
    REQUIRE(rollCallA.getNodeId() != rollCallB.getNodeId());
    
    // Both should know about each other
    auto aMapping = rollCallA.getNameToIdMap();
    auto bMapping = rollCallB.getNameToIdMap();
    
    REQUIRE(aMapping.count("node-beta") == 1);
    REQUIRE(bMapping.count("node-alpha") == 1);
}

TEST_CASE("RollCall local cache lookup", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "test-node", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "remote-sensor", getTimeMock, sleepMock, getTestRandom2);
    
    // Initialize nodes and let them learn about each other
    REQUIRE(rollCallA.begin() == true);
    rollCallB.processMessages(100);  // B learns about A
    REQUIRE(rollCallB.begin() == true);
    rollCallA.processMessages(100);  // A learns about B
    
    // Now test that local cache works (should find it immediately without network)
    uint16_t cachedId = rollCallA.whoIs("remote-sensor", 10); // Short timeout - should use cache
    REQUIRE(cachedId == rollCallB.getNodeId());
    
    std::string cachedName = rollCallA.whereIs(rollCallB.getNodeId(), 10); // Short timeout - should use cache
    REQUIRE(cachedName == "remote-sensor");
}

TEST_CASE("RollCall message parsing", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, 2, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "parser-test", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "target-node", getTimeMock, sleepMock, getTestRandom2);
    
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    
    // Test that malformed messages are ignored
    std::string badMsg1 = "INVALID|test";
    std::string badMsg2 = "HELLOIAM|"; // Empty content
    std::string badMsg3 = "HELLOIAM|name"; // Missing AT clause
    
    radioA.send(reinterpret_cast<const uint8_t*>(badMsg1.c_str()), badMsg1.length());
    radioA.send(reinterpret_cast<const uint8_t*>(badMsg2.c_str()), badMsg2.length());
    radioA.send(reinterpret_cast<const uint8_t*>(badMsg3.c_str()), badMsg3.length());
    
    // These should not crash and should return false
    REQUIRE(rollCallB.processMessages(100) == false);
    REQUIRE(rollCallB.processMessages(100) == false);
    REQUIRE(rollCallB.processMessages(100) == false);
    
    // The mapping should still only contain the target node's own entry
    auto mapping = rollCallB.getNameToIdMap();
    REQUIRE(mapping.size() == 1);
    REQUIRE(mapping.count("target-node") == 1);
}

TEST_CASE("RollCall timeout handling", "[RollCall]") {
    MockRadio radioA;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, 1, getTimeMock, sleepMock);
    RollCall rollCallA(&linkA, "timeout-test", getTimeMock, sleepMock, getTestRandom1);
    
    REQUIRE(rollCallA.begin() == true);
    
    // Query for a non-existent node with short timeout
    uint16_t result = rollCallA.whoIs("non-existent", 100);
    REQUIRE(result == 0);
    
    std::string nameResult = rollCallA.whereIs(9999, 100);
    REQUIRE(nameResult == "");
}