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
    // Two nodes with the same ID: both apply the same rule, so exactly one of
    // them moves - the one whose name sorts later.
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    fakeTime = 0;

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    RollCall rollCallA(&linkA, "node-mike", getTimeMock, sleepMock, getTestRandom1);
    REQUIRE(rollCallA.begin() == true);
    const uint16_t idA = rollCallA.getNodeId();
    const std::string id = std::to_string(idA);

    SECTION("The announcing node's name sorts earlier: we give up the ID") {
        REQUIRE(rollCallA.processRollCallMessage("HELLOIAM|node-alpha AT " + id + " #7", idA) == true);

        const uint16_t newIdA = rollCallA.getNodeId();
        REQUIRE(newIdA != idA);
        REQUIRE(newIdA != 0);
        REQUIRE(newIdA != 0xFFFF);
        REQUIRE(rollCallA.getNodeName() == "node-mike");

        // Our table has us at the new ID and the other node at the old one.
        const auto& byName = rollCallA.getNameToIdMap();
        const auto& byId = rollCallA.getIdToNameMap();
        REQUIRE(byName.at("node-mike") == newIdA);
        REQUIRE(byName.at("node-alpha") == idA);
        REQUIRE(byId.at(idA) == "node-alpha");
        REQUIRE(byId.at(newIdA) == "node-mike");
        REQUIRE(byId.size() == 2);

        // The new identity is announced within a second.
        MockRadio::clearChannel();
        fakeTime += 700;
        rollCallA.processMessages(10);
        uint8_t raw[256];
        int len = radioB.receive(raw, 256);
        REQUIRE(len > 9);
        std::string payload(reinterpret_cast<char*>(raw) + 7, len - 9);
        REQUIRE(payload.rfind("HELLOIAM|node-mike AT " + std::to_string(newIdA), 0) == 0);
        // And the link layer now filters on the new ID.
        REQUIRE(((raw[2] << 8) | raw[3]) == newIdA);
    }

    SECTION("The announcing node's name sorts later: we keep the ID and say so") {
        REQUIRE(rollCallA.processRollCallMessage("HELLOIAM|node-zulu AT " + id + " #7", idA) == true);
        REQUIRE(rollCallA.getNodeId() == idA);
        REQUIRE(rollCallA.getNameToIdMap().count("node-zulu") == 0);   // it does not get our ID

        // We re-announce soon so that the other node notices the collision.
        MockRadio::clearChannel();
        fakeTime += 3100;
        rollCallA.processMessages(10);
        uint8_t raw[256];
        int len = radioB.receive(raw, 256);
        REQUIRE(len > 9);
        std::string payload(reinterpret_cast<char*>(raw) + 7, len - 9);
        REQUIRE(payload.rfind("HELLOIAM|node-mike AT " + id, 0) == 0);
    }

    SECTION("Two live nodes resolve an ID collision between themselves") {
        // B is forced to pick A's ID.
        static uint16_t forcedId;
        static int calls;
        forcedId = idA;
        calls = 0;
        auto sameIdFirst = []() -> uint16_t { return calls++ == 0 ? forcedId : getTestRandom2(); };
        LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
        RollCall rollCallB(&linkB, "node-beta", getTimeMock, sleepMock, sameIdFirst);
        REQUIRE(rollCallB.begin() == true);

        for (int i = 0; i < 100; ++i) {
            rollCallA.processMessages(10);
            rollCallB.processMessages(10);
            fakeTime += 100;
        }
        // "node-beta" sorts before "node-mike": B keeps the ID, A moves.
        REQUIRE(rollCallB.getNodeId() == idA);
        REQUIRE(rollCallA.getNodeId() != idA);
        REQUIRE(rollCallA.getNameToIdMap().at("node-beta") == idA);
        REQUIRE(rollCallB.getNameToIdMap().at("node-mike") == rollCallA.getNodeId());
        REQUIRE(rollCallA.getIdToNameMap().size() == 2);
        REQUIRE(rollCallB.getIdToNameMap().size() == 2);
    }
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
    REQUIRE(rollCallB.begin() == true);   // B hears A's announcement while starting
    
    // Malformed messages, sent as proper link frames by A, are ignored by B
    const std::string bad[] = {
        "INVALID|test",                 // not a RollCall message at all
        "HELLOIAM|",                    // empty content
        "HELLOIAM|name",                // missing AT clause
        "HELLOIAM|name AT ",            // missing ID
        "HELLOIAM|name AT abc",         // ID is not a number (crashed version 1)
        "HELLOIAM|name AT 99999999999", // ID out of range (crashed version 1)
        "HELLOIAM|name AT 0",           // reserved ID
        "HELLOIAM|name AT 65535",       // broadcast address
        "WHEREIS|not-a-number",         // (crashed version 1)
        "RESP|name AT",
    };
    for (const auto& text : bad) {
        INFO(text);
        REQUIRE(linkA.sendPacket(rollCallA.getNodeId(), BROADCAST_ADDR,
                                 reinterpret_cast<const uint8_t*>(text.data()),
                                 static_cast<uint8_t>(text.size())) == true);
        REQUIRE(rollCallB.processMessages(100) == false);
    }
    
    // B knows itself and A, and nothing from the malformed messages
    const auto& mapping = rollCallB.getNameToIdMap();
    REQUIRE(mapping.size() == 2);
    REQUIRE(mapping.count("target-node") == 1);
    REQUIRE(mapping.count("parser-test") == 1);
    REQUIRE(mapping.count("name") == 0);

    // Well formed announcements are accepted with or without the nonce
    REQUIRE(rollCallB.processRollCallMessage("HELLOIAM|old-style AT 4242", 4242) == true);
    REQUIRE(rollCallB.processRollCallMessage("HELLOIAM|new-style AT 4243 #17", 4243) == true);
    REQUIRE(rollCallB.getNameToIdMap().at("old-style") == 4242);
    REQUIRE(rollCallB.getNameToIdMap().at("new-style") == 4243);
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
    
    // Initialize both nodes - this generates HELLOIAM messages
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);

    bool hasHelloSent = false;
    bool hasHelloReceived = false;
    for (const auto& msg : testLogMessages) {
        if (msg.find("[RollCall] Sending: HELLOIAM|log-test-a AT ") == 0) hasHelloSent = true;
        if (msg.find("[RollCall] Received: HELLOIAM|log-test-a AT ") == 0 && msg.find(" from ID ") != std::string::npos) hasHelloReceived = true;
    }
    REQUIRE(hasHelloSent == true);
    REQUIRE(hasHelloReceived == true);
    
    // Clear log messages after initialization to focus on query/response
    testLogMessages.clear();
    MockRadio::clearChannel();
    
    // A asks for a name it does not know; B hears the question
    uint16_t result = rollCallA.whoIs("log-test-c", 100);
    REQUIRE(result == 0);
    rollCallB.processMessages(100);
    
    bool hasWhoisSent = false;
    bool hasWhoisReceived = false;
    for (const auto& msg : testLogMessages) {
        if (msg == "[RollCall] Sending: WHOIS|log-test-c") {
            hasWhoisSent = true;
        }
        if (msg.find("[RollCall] Received: WHOIS|log-test-c from ID " + std::to_string(rollCallA.getNodeId())) == 0) {
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

TEST_CASE("RollCall name deconfliction", "[RollCall]") {
    // Two nodes with the same name: exactly one of them renames itself - the
    // one with the larger ID. (Version 1 renamed whichever node heard the
    // other first, and often both.)
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    fakeTime = 0;
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    RollCall rollCallA(&linkA, "node-alpha", getTimeMock, sleepMock, getTestRandom1);
    REQUIRE(rollCallA.begin() == true);
    const uint16_t idA = rollCallA.getNodeId();
    REQUIRE(idA > 1);
    REQUIRE(idA < 0xFFFE);

    SECTION("The other node has the smaller ID: we rename ourselves") {
        const uint16_t other = static_cast<uint16_t>(idA - 1);
        REQUIRE(rollCallA.processRollCallMessage("HELLOIAM|node-alpha AT " + std::to_string(other), other) == true);

        REQUIRE(rollCallA.getNodeId() == idA);                         // ID unchanged
        const std::string newName = rollCallA.getNodeName();
        REQUIRE(newName != "node-alpha");
        REQUIRE(newName.rfind("node-alpha-", 0) == 0);                 // base name plus a random suffix
        REQUIRE(RollCall::isValidName(newName));

        const auto& byName = rollCallA.getNameToIdMap();
        REQUIRE(byName.at("node-alpha") == other);                     // the name now belongs to the other node
        REQUIRE(byName.at(newName) == idA);
        REQUIRE(byName.size() == 2);
        REQUIRE(rollCallA.getIdToNameMap().size() == 2);
    }

    SECTION("The other node has the larger ID: we keep the name") {
        const uint16_t other = static_cast<uint16_t>(idA + 1);
        REQUIRE(rollCallA.processRollCallMessage("HELLOIAM|node-alpha AT " + std::to_string(other), other) == true);
        REQUIRE(rollCallA.getNodeName() == "node-alpha");
        REQUIRE(rollCallA.getNodeId() == idA);
        REQUIRE(rollCallA.getNameToIdMap().at("node-alpha") == idA);
        REQUIRE(rollCallA.getNameToIdMap().size() == 1);
    }

    SECTION("A very long name is shortened to make room for the suffix") {
        MockRadio radioC;
        LoRaBasicLink linkC(&radioC, getTimeMock, sleepMock);
        const std::string longName(ROLLCALL_MAX_NAME_LEN, 'n');
        RollCall rollCallC(&linkC, longName, getTimeMock, sleepMock, getTestRandom2);
        REQUIRE(rollCallC.begin() == true);
        const uint16_t other = static_cast<uint16_t>(rollCallC.getNodeId() - 1);
        REQUIRE(rollCallC.processRollCallMessage("HELLOIAM|" + longName + " AT " + std::to_string(other), other) == true);
        REQUIRE(rollCallC.getNodeName() != longName);
        REQUIRE(rollCallC.getNodeName().size() <= ROLLCALL_MAX_NAME_LEN);
        REQUIRE(RollCall::isValidName(rollCallC.getNodeName()));
    }
}

TEST_CASE("RollCall bidirectional name collision resolution", "[RollCall]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    fakeTime = 0;
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    // Both nodes start with the same name
    RollCall rollCallA(&linkA, "duplicate-name", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "duplicate-name", getTimeMock, sleepMock, getTestRandom2);
    
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    REQUIRE(idA != idB);

    // Let the two nodes run for a while
    for (int i = 0; i < 100; ++i) {
        rollCallA.processMessages(10);
        rollCallB.processMessages(10);
        fakeTime += 100;
    }
    
    // Exactly one of them renamed itself: the one with the larger ID
    RollCall& keeper = (idA < idB) ? rollCallA : rollCallB;
    RollCall& renamed = (idA < idB) ? rollCallB : rollCallA;
    REQUIRE(keeper.getNodeName() == "duplicate-name");
    REQUIRE(renamed.getNodeName() != "duplicate-name");
    REQUIRE(renamed.getNodeName().rfind("duplicate-name-", 0) == 0);
    
    // IDs remain the same
    REQUIRE(rollCallA.getNodeId() == idA);
    REQUIRE(rollCallB.getNodeId() == idB);
    
    // Both know each other's final names, from the table (no query needed)
    REQUIRE(rollCallA.whoIs(rollCallB.getNodeName(), 10) == idB);
    REQUIRE(rollCallB.whoIs(rollCallA.getNodeName(), 10) == idA);
    REQUIRE(rollCallA.getNameToIdMap().size() == 2);
    REQUIRE(rollCallB.getNameToIdMap().size() == 2);
}

TEST_CASE("RollCall random number generation uniqueness", "[RollCall]") {
    MockRadio radioA, radioB, radioC;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    LoRaBasicLink linkC(&radioC, getTimeMock, sleepMock);
    
    // Create multiple RollCall instances without custom random functions
    // They should get different IDs due to improved seeding
    RollCall rollCallA(&linkA, "node-a", getTimeMock, sleepMock);
    RollCall rollCallB(&linkB, "node-b", getTimeMock, sleepMock);
    RollCall rollCallC(&linkC, "node-c", getTimeMock, sleepMock);
    
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    REQUIRE(rollCallC.begin() == true);
    
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    uint16_t idC = rollCallC.getNodeId();
    
    // All IDs should be different (very high probability with good randomness)
    REQUIRE(idA != idB);
    REQUIRE(idB != idC);
    REQUIRE(idA != idC);
    
    // All IDs should be valid (not reserved values)
    REQUIRE(idA != 0);
    REQUIRE(idA != 0xFFFF);
    REQUIRE(idB != 0);
    REQUIRE(idB != 0xFFFF);
    REQUIRE(idC != 0);
    REQUIRE(idC != 0xFFFF);
}

TEST_CASE("RollCall complete collision detection and resolution", "[RollCall]") {
    // "Complete collision": another node announces our name AND our ID. The
    // link layer source ID is the same as ours too, so the only thing that
    // tells the two nodes apart is the random nonce in the announcement.
    MockRadio radioA;
    MockRadio::clearChannel();
    fakeTime = 0;

    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);

    // First value is the node ID, second the nonce, the rest varies.
    static int valueIndex;
    valueIndex = 0;
    auto deterministicRandom = []() -> uint16_t {
        int i = valueIndex++;
        if (i == 0) return 0x1234;
        if (i == 1) return 0x0100;       // nonce = 256
        return static_cast<uint16_t>(0x5000 + i * 37);
    };

    RollCall rollCallA(&linkA, "duplicate-node", getTimeMock, sleepMock, deterministicRandom);
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallA.getNodeId() == 0x1234);
    const std::string claim = "HELLOIAM|duplicate-node AT " + std::to_string(0x1234);

    SECTION("The other node has the smaller nonce: we change both name and ID") {
        REQUIRE(rollCallA.processRollCallMessage(claim + " #255", 0x1234) == true);
        REQUIRE(rollCallA.getNodeId() != 0x1234);
        REQUIRE(rollCallA.getNodeName() != "duplicate-node");
        REQUIRE(rollCallA.getNodeName().find("duplicate-node-") == 0);

        // The original identity now belongs to the other node.
        const auto& byName = rollCallA.getNameToIdMap();
        REQUIRE(byName.at("duplicate-node") == 0x1234);
        REQUIRE(byName.at(rollCallA.getNodeName()) == rollCallA.getNodeId());
        REQUIRE(byName.size() == 2);
        REQUIRE(rollCallA.getIdToNameMap().size() == 2);
    }

    SECTION("The other node has the larger nonce: we keep our identity") {
        REQUIRE(rollCallA.processRollCallMessage(claim + " #257", 0x1234) == true);
        REQUIRE(rollCallA.getNodeId() == 0x1234);
        REQUIRE(rollCallA.getNodeName() == "duplicate-node");
        REQUIRE(rollCallA.getNameToIdMap().size() == 1);
    }

    SECTION("Our own nonce: it is our own announcement coming back, nothing changes") {
        REQUIRE(rollCallA.processRollCallMessage(claim + " #256", 0x1234) == true);
        REQUIRE(rollCallA.getNodeId() == 0x1234);
        REQUIRE(rollCallA.getNodeName() == "duplicate-node");
        REQUIRE(rollCallA.getNameToIdMap().size() == 1);
    }

    SECTION("No nonce (a version 1 node): we give way") {
        REQUIRE(rollCallA.processRollCallMessage(claim, 0x0777) == true);
        REQUIRE(rollCallA.getNodeId() != 0x1234);
        REQUIRE(rollCallA.getNodeName() != "duplicate-node");
        REQUIRE(rollCallA.getNameToIdMap().at("duplicate-node") == 0x1234);
    }
}

TEST_CASE("RollCall survives a random source that is stuck", "[RollCall]") {
    // A broken random number generator must not hang the node in an endless
    // "pick another ID" loop.
    MockRadio radioA;
    MockRadio::clearChannel();
    fakeTime = 0;
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);

    SECTION("Always the same value") {
        auto stuck = []() -> uint16_t { return 0x4242; };
        RollCall rollCall(&linkA, "stuck", getTimeMock, sleepMock, stuck);
        REQUIRE(rollCall.begin() == true);
        REQUIRE(rollCall.getNodeId() == 0x4242);
        // ID collision with a name that sorts earlier: we must still get a different, valid ID.
        REQUIRE(rollCall.processRollCallMessage("HELLOIAM|aaa AT " + std::to_string(0x4242), 0x4242) == true);
        REQUIRE(rollCall.getNodeId() != 0x4242);
        REQUIRE(rollCall.getNodeId() != 0);
        REQUIRE(rollCall.getNodeId() != 0xFFFF);
        // Name collision: the suffix generator also terminates.
        uint16_t other = static_cast<uint16_t>(rollCall.getNodeId() - 1);
        REQUIRE(rollCall.processRollCallMessage("HELLOIAM|stuck AT " + std::to_string(other), other) == true);
        REQUIRE(rollCall.getNodeName() != "stuck");
    }

    SECTION("Always a reserved value") {
        auto zero = []() -> uint16_t { return 0; };
        RollCall rollCall(&linkA, "zero", getTimeMock, sleepMock, zero);
        REQUIRE(rollCall.begin() == true);
        REQUIRE(rollCall.getNodeId() != 0);
        REQUIRE(rollCall.getNodeId() != 0xFFFF);

        auto ones = []() -> uint16_t { return 0xFFFF; };
        RollCall rollCall2(&linkA, "ones", getTimeMock, sleepMock, ones);
        REQUIRE(rollCall2.begin() == true);
        REQUIRE(rollCall2.getNodeId() != 0);
        REQUIRE(rollCall2.getNodeId() != 0xFFFF);
    }
}
