#include <catch2/catch_test_macros.hpp>
#include "RollCall.h"
#include "LoraBasicLink.h"
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
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
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
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "node-a", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "node-b", getTimeMock, sleepMock, getTestRandom2);
    
    // Initialize nodes (this will assign them their IDs)
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    
    // Get the actual IDs assigned
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    
    // Clear any messages from begin()
    MockRadio::clearChannel();
    
    // Manually send HELLOIAM messages to simulate the broadcast
    std::string helloA = "HELLOIAM|node-a AT " + std::to_string(idA);
    std::string helloB = "HELLOIAM|node-b AT " + std::to_string(idB);
    
    // Send A's message for B to receive
    REQUIRE(linkA.sendPacket(idA, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloA.c_str()), 
                            helloA.length()) == true);
    REQUIRE(rollCallB.processMessages(100) == true);
    
    // Send B's message for A to receive  
    REQUIRE(linkB.sendPacket(idB, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloB.c_str()), 
                            helloB.length()) == true);
    REQUIRE(rollCallA.processMessages(100) == true);
    
    // Check that they learned about each other
    auto aNameToId = rollCallA.getNameToIdMap();
    auto bNameToId = rollCallB.getNameToIdMap();
    
    REQUIRE(aNameToId.count("node-b") == 1);
    REQUIRE(aNameToId["node-b"] == idB);
    
    REQUIRE(bNameToId.count("node-a") == 1);
    REQUIRE(bNameToId["node-a"] == idA);
}

TEST_CASE("RollCall WHOIS query", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "sensor-1", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "gateway-1", getTimeMock, sleepMock, getTestRandom2);
    
    // Initialize nodes and let them learn about each other
    REQUIRE(rollCallA.begin() == true);
    rollCallB.processMessages(100);  // B learns about A
    REQUIRE(rollCallB.begin() == true);
    rollCallA.processMessages(100);  // A learns about B
    
    // Get the node IDs
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    
    // Clear the channel
    MockRadio::clearChannel();
    
    // Test WHOIS query from A to find B
    std::string queryName = "gateway-1";
    
    // Start the query (this broadcasts the WHOIS message)
    uint16_t foundId = 0;
    
    // Since we can't do this in parallel with the mock, we'll simulate the exchange
    // A sends WHOIS query
    std::string query = std::string("WHOIS|") + queryName;
    REQUIRE(linkA.sendPacket(idA, BROADCAST_ADDR, 
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
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
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
    REQUIRE(linkA.sendPacket(rollCallA.getNodeId(), BROADCAST_ADDR, 
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
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Use a custom random function that returns the same ID initially for A, then different values
    static int callCount = 0;
    auto collisionRandom = []() { 
        callCount++;
        if (callCount == 1) {
            return static_cast<uint16_t>(0x1234); // First call returns fixed ID
        } else {
            return static_cast<uint16_t>(0x5678); // Subsequent calls return different ID
        }
    };
    
    RollCall rollCallA(&linkA, "node-alpha", getTimeMock, sleepMock, collisionRandom);
    RollCall rollCallB(&linkB, "node-beta", getTimeMock, sleepMock, getTestRandom2);
    
    // Initialize both nodes
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    
    // Clear messages from begin()
    MockRadio::clearChannel();
    
    // Simulate collision: B announces with A's ID
    std::string conflictMessage = "HELLOIAM|node-beta AT " + std::to_string(idA);
    REQUIRE(linkB.sendPacket(idB, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(conflictMessage.c_str()), 
                            conflictMessage.length()) == true);
    
    // A processes the conflicting message - should trigger collision handling
    REQUIRE(rollCallA.processMessages(100) == true);
    
    // A should have changed its ID due to the collision
    uint16_t newIdA = rollCallA.getNodeId();
    REQUIRE(newIdA != idA); // A should have a new ID
    
    // Clear channel again
    MockRadio::clearChannel();
    
    // Now let them properly exchange their final identities
    std::string helloNewA = "HELLOIAM|node-alpha AT " + std::to_string(newIdA);
    std::string helloB = "HELLOIAM|node-beta AT " + std::to_string(idB);
    
    // Exchange messages
    REQUIRE(linkA.sendPacket(newIdA, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloNewA.c_str()), 
                            helloNewA.length()) == true);
    REQUIRE(rollCallB.processMessages(100) == true);
    
    REQUIRE(linkB.sendPacket(idB, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloB.c_str()), 
                            helloB.length()) == true);
    REQUIRE(rollCallA.processMessages(100) == true);
    
    // Both should know about each other with correct IDs
    auto aMapping = rollCallA.getNameToIdMap();
    auto bMapping = rollCallB.getNameToIdMap();
    
    REQUIRE(aMapping.count("node-beta") == 1);
    REQUIRE(aMapping["node-beta"] == idB);
    
    REQUIRE(bMapping.count("node-alpha") == 1);
    REQUIRE(bMapping["node-alpha"] == newIdA);
}

TEST_CASE("RollCall local cache lookup", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "test-node", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "remote-sensor", getTimeMock, sleepMock, getTestRandom2);
    
    // Initialize nodes and let them learn about each other using manual message exchange
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    
    // Get the actual IDs assigned
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    
    // Clear any messages from begin()
    MockRadio::clearChannel();
    
    // Exchange HELLOIAM messages
    std::string helloA = "HELLOIAM|test-node AT " + std::to_string(idA);
    std::string helloB = "HELLOIAM|remote-sensor AT " + std::to_string(idB);
    
    // A learns about B
    REQUIRE(linkB.sendPacket(idB, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloB.c_str()), 
                            helloB.length()) == true);
    REQUIRE(rollCallA.processMessages(100) == true);
    
    // Now test that local cache works (should find it immediately without network)
    uint16_t cachedId = rollCallA.whoIs("remote-sensor", 10); // Short timeout - should use cache
    REQUIRE(cachedId == idB);
    
    std::string cachedName = rollCallA.whereIs(idB, 10); // Short timeout - should use cache
    REQUIRE(cachedName == "remote-sensor");
}

TEST_CASE("RollCall message parsing", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
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
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    RollCall rollCallA(&linkA, "timeout-test", getTimeMock, sleepMock, getTestRandom1);
    
    REQUIRE(rollCallA.begin() == true);
    
    // Query for a non-existent node with short timeout
    uint16_t result = rollCallA.whoIs("non-existent", 100);
    REQUIRE(result == 0);
    
    std::string nameResult = rollCallA.whereIs(9999, 100);
    REQUIRE(nameResult == "");
}

TEST_CASE("RollCall periodic HELLOIAM rebroadcast functionality", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    fakeTime = 0; // Reset time
    
    RollCall rollCallA(&linkA, "test-periodic", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "test-listener", getTimeMock, sleepMock, getTestRandom2);
    
    // Initialize both nodes
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    
    // Clear all initial messages from begin()
    MockRadio::clearChannel();
    
    // Test that periodic announcement is triggered after the interval
    fakeTime = 35000; // 35 seconds - should trigger the 30-second interval
    
    // A should send periodic announcement
    rollCallA.processMessages(100);
    
    // B should be able to receive the announcement
    bool received = rollCallB.processMessages(100);
    
    // Verify that a message was sent (B received something)
    REQUIRE(received == true);
    
    // Test that announcement is NOT triggered before the interval (timing test)
    fakeTime = 40000; // Only 5 seconds later, should not trigger again yet
    MockRadio::clearChannel(); // Clear the previous message
    
    rollCallA.processMessages(100);
    bool receivedTooEarly = rollCallB.processMessages(100);
    REQUIRE(receivedTooEarly == false); // Should be no new message
    
    // Test that announcement IS triggered after another full interval
    fakeTime = 70000; // 30+ seconds later, should trigger again
    
    rollCallA.processMessages(100);
    bool receivedAgain = rollCallB.processMessages(100);
    REQUIRE(receivedAgain == true); // Should be a new message
}

TEST_CASE("RollCall message logging", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    // Use a global variable for capturing log messages in test
    static std::vector<std::string> testLogMessages;
    testLogMessages.clear();
    
    auto testLogger = [](const char* message) {
        testLogMessages.push_back(std::string(message));
    };
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Create RollCall instances with logging enabled
    RollCall rollCallA(&linkA, "log-test-a", getTimeMock, sleepMock, getTestRandom1, testLogger);
    RollCall rollCallB(&linkB, "log-test-b", getTimeMock, sleepMock, getTestRandom2, testLogger);
    
    // Initialize both nodes - this should generate HELLOIAM messages
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    
    // Clear log messages after initialization to focus on query/response
    testLogMessages.clear();
    
    // Let A learn about B first (this is important for the test to work)
    rollCallA.processMessages(100);  // A processes B's announcement
    
    // Now test a WHOIS query - A should know about B and can query for it  
    uint16_t result = rollCallA.whoIs("log-test-b", 100);
    
    // Process any remaining messages
    rollCallB.processMessages(100);
    rollCallA.processMessages(100);
    
    // Check that we have log messages
    REQUIRE(testLogMessages.size() > 0);
    
    // Check for specific message types in logs
    bool hasWhoisSent = false;
    bool hasWhoisReceived = false;
    
    for (const auto& msg : testLogMessages) {
        if (msg.find("WHOIS|") != std::string::npos && msg.find("Sending:") != std::string::npos) {
            hasWhoisSent = true;
        }
        if (msg.find("WHOIS|") != std::string::npos && msg.find("Received:") != std::string::npos) {
            hasWhoisReceived = true;
        }
    }
    
    REQUIRE(hasWhoisSent == true);
    REQUIRE(hasWhoisReceived == true);
}

TEST_CASE("RollCall logging disabled by default", "[RollCall]") {
    MockRadio radioA;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    
    // Create RollCall without logging function (should work fine)
    RollCall rollCallA(&linkA, "no-log-test", getTimeMock, sleepMock, getTestRandom1);
    
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallA.getNodeName() == "no-log-test");
    
    // This should work fine without any logging
    rollCallA.whoIs("nonexistent", 50);
}