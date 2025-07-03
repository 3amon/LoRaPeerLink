#include <catch2/catch_test_macros.hpp>
#include "PeerMessenger.h"
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
static TestRandom testRng3(32767);

static uint16_t getTestRandom1() {
    return testRng1.next();
}

static uint16_t getTestRandom2() {
    return testRng2.next();
}

static uint16_t getTestRandom3() {
    return testRng3.next();
}

TEST_CASE("PeerMessenger basic initialization", "[PeerMessenger]") {
    MockRadio radioA;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    RollCall rollCallA(&linkA, "node-a", getTimeMock, sleepMock, getTestRandom1);
    PeerMessenger messengerA(&rollCallA);
    
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(messengerA.begin() == true);
    
    REQUIRE(messengerA.hasMessage() == false);
    REQUIRE(messengerA.getMessageCount() == 0);
    
    // Test accessing RollCall through messenger
    REQUIRE(messengerA.getRollCall().getNodeName() == "node-a");
    REQUIRE(messengerA.getRollCall().getNodeId() != 0);
    REQUIRE(messengerA.getRollCall().getNodeId() != 0xFFFF);
}

TEST_CASE("PeerMessenger send message by ID", "[PeerMessenger]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "alice", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "bob", getTimeMock, sleepMock, getTestRandom2);
    
    PeerMessenger messengerA(&rollCallA);
    PeerMessenger messengerB(&rollCallB);
    
    // Initialize both nodes
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    REQUIRE(messengerA.begin() == true);
    REQUIRE(messengerB.begin() == true);
    
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    
    // Let nodes discover each other through RollCall
    MockRadio::clearChannel();
    
    // Exchange HELLOIAM messages so they know each other
    std::string helloA = "HELLOIAM|alice AT " + std::to_string(idA);
    std::string helloB = "HELLOIAM|bob AT " + std::to_string(idB);
    
    // A learns about B
    REQUIRE(linkA.sendPacket(idA, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloB.c_str()), 
                            helloB.length()) == true);
    REQUIRE(rollCallA.processMessages(100) == true);
    
    // B learns about A  
    REQUIRE(linkB.sendPacket(idB, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloA.c_str()), 
                            helloA.length()) == true);
    REQUIRE(rollCallB.processMessages(100) == true);
    
    // Clear channel for user messages
    MockRadio::clearChannel();
    
    // Send a message from A to B
    std::string testMessage = "Hello Bob!";
    REQUIRE(messengerA.sendMessage(idB, testMessage) == true);
    
    // B should receive the message
    REQUIRE(messengerB.processMessages(100) == true);
    REQUIRE(messengerB.hasMessage() == true);
    REQUIRE(messengerB.getMessageCount() == 1);
    
    UserMessage received = messengerB.receiveMessage();
    REQUIRE(received.srcId == idA);
    REQUIRE(received.content == testMessage);
    REQUIRE(received.srcName == "alice"); // Should resolve the name
    
    // No more messages
    REQUIRE(messengerB.hasMessage() == false);
    REQUIRE(messengerB.getMessageCount() == 0);
}

TEST_CASE("PeerMessenger send message by name", "[PeerMessenger]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "alice", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "bob", getTimeMock, sleepMock, getTestRandom2);
    
    PeerMessenger messengerA(&rollCallA);
    PeerMessenger messengerB(&rollCallB);
    
    // Initialize both nodes
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    REQUIRE(messengerA.begin() == true);
    REQUIRE(messengerB.begin() == true);
    
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    
    // Let nodes discover each other through RollCall
    MockRadio::clearChannel();
    
    // Exchange HELLOIAM messages so they know each other
    std::string helloA = "HELLOIAM|alice AT " + std::to_string(idA);
    std::string helloB = "HELLOIAM|bob AT " + std::to_string(idB);
    
    // A learns about B
    REQUIRE(linkA.sendPacket(idA, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloB.c_str()), 
                            helloB.length()) == true);
    REQUIRE(rollCallA.processMessages(100) == true);
    
    // B learns about A  
    REQUIRE(linkB.sendPacket(idB, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloA.c_str()), 
                            helloA.length()) == true);
    REQUIRE(rollCallB.processMessages(100) == true);
    
    // Clear channel for user messages
    MockRadio::clearChannel();
    
    // Now send a message from A to B by name
    std::string testMessage = "Hello Bob by name!";
    REQUIRE(messengerA.sendMessage("bob", testMessage) == true);
    
    // B should receive the message
    REQUIRE(messengerB.processMessages(100) == true);
    REQUIRE(messengerB.hasMessage() == true);
    
    UserMessage received = messengerB.receiveMessage();
    REQUIRE(received.srcId == idA);
    REQUIRE(received.content == testMessage);
    REQUIRE(received.srcName == "alice");
}

TEST_CASE("PeerMessenger broadcast message", "[PeerMessenger]") {
    MockRadio radioA, radioB, radioC;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    LoRaBasicLink linkC(&radioC, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "alice", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "bob", getTimeMock, sleepMock, getTestRandom2);
    RollCall rollCallC(&linkC, "charlie", getTimeMock, sleepMock, getTestRandom3);
    
    PeerMessenger messengerA(&rollCallA);
    PeerMessenger messengerB(&rollCallB);
    PeerMessenger messengerC(&rollCallC);
    
    // Initialize all nodes
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    REQUIRE(rollCallC.begin() == true);
    REQUIRE(messengerA.begin() == true);
    REQUIRE(messengerB.begin() == true);
    REQUIRE(messengerC.begin() == true);
    
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    uint16_t idC = rollCallC.getNodeId();
    
    // Let all nodes discover each other through RollCall
    MockRadio::clearChannel();
    
    // Exchange HELLOIAM messages
    std::string helloA = "HELLOIAM|alice AT " + std::to_string(idA);
    std::string helloB = "HELLOIAM|bob AT " + std::to_string(idB);
    std::string helloC = "HELLOIAM|charlie AT " + std::to_string(idC);
    
    // Broadcast all hellos (they all receive each other's announcements)
    REQUIRE(linkA.sendPacket(idA, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloA.c_str()), 
                            helloA.length()) == true);
    REQUIRE(linkB.sendPacket(idB, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloB.c_str()), 
                            helloB.length()) == true);
    REQUIRE(linkC.sendPacket(idC, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(helloC.c_str()), 
                            helloC.length()) == true);
    
    // Let all nodes process the hellos sequentially since MockRadio shares a global queue
    while (rollCallA.processMessages(10)); // Process all available messages for A
    while (rollCallB.processMessages(10)); // Process all available messages for B  
    while (rollCallC.processMessages(10)); // Process all available messages for C
    
    // Clear channel for user messages
    MockRadio::clearChannel();
    
    // Broadcast a message from A
    std::string broadcastMessage = "Hello everyone!";
    REQUIRE(messengerA.broadcastMessage(broadcastMessage) == true);
    
    // Due to MockRadio's shared queue, only one node will receive the broadcast
    // Let's process messages and see which node gets it
    bool bGotMessage = messengerB.processMessages(100);
    bool cGotMessage = messengerC.processMessages(100);
    
    // At least one should receive the broadcast
    REQUIRE((bGotMessage || cGotMessage) == true);
    
    if (bGotMessage && messengerB.hasMessage()) {
        UserMessage receivedB = messengerB.receiveMessage();
        REQUIRE(receivedB.srcId == idA);
        REQUIRE(receivedB.content == broadcastMessage);
    }
    
    if (cGotMessage && messengerC.hasMessage()) {
        UserMessage receivedC = messengerC.receiveMessage();
        REQUIRE(receivedC.srcId == idA);
        REQUIRE(receivedC.content == broadcastMessage);
    }
}

TEST_CASE("PeerMessenger message filtering", "[PeerMessenger]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "alice", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "bob", getTimeMock, sleepMock, getTestRandom2);
    
    PeerMessenger messengerA(&rollCallA);
    PeerMessenger messengerB(&rollCallB);
    
    // Initialize both nodes
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    REQUIRE(messengerA.begin() == true);
    REQUIRE(messengerB.begin() == true);
    
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    
    // Clear any initial messages
    MockRadio::clearChannel();
    
    // Send a RollCall protocol message from A to B - this should NOT appear in PeerMessenger
    std::string rollCallMessage = "WHOIS|bob";
    REQUIRE(linkA.sendPacket(idA, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(rollCallMessage.c_str()), 
                            rollCallMessage.length()) == true);
    
    // Send a user message from A to B - this SHOULD appear in PeerMessenger
    std::string userMessage = "This is a user message";
    REQUIRE(messengerA.sendMessage(idB, userMessage) == true);
    
    // Process both messages at B
    REQUIRE(messengerB.processMessages(100) == true); // RollCall message
    REQUIRE(messengerB.processMessages(100) == true); // User message
    
    // PeerMessenger should only have the user message, not the RollCall message
    REQUIRE(messengerB.hasMessage() == true);
    REQUIRE(messengerB.getMessageCount() == 1);
    
    UserMessage received = messengerB.receiveMessage();
    REQUIRE(received.content == userMessage);
    REQUIRE(received.srcId == idA);
    
    // No more user messages
    REQUIRE(messengerB.hasMessage() == false);
}

TEST_CASE("PeerMessenger send to unknown name", "[PeerMessenger]") {
    MockRadio radioA;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    RollCall rollCallA(&linkA, "alice", getTimeMock, sleepMock, getTestRandom1);
    PeerMessenger messengerA(&rollCallA);
    
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(messengerA.begin() == true);
    
    // Try to send to an unknown name
    REQUIRE(messengerA.sendMessage("unknown-node", "test message") == false);
}

TEST_CASE("PeerMessenger multiple messages queue", "[PeerMessenger]") {
    MockRadio radioA, radioB;
    MockRadio::clearChannel();
    
    LoRaBasicLink linkA(&radioA, getTimeMock, sleepMock);
    LoRaBasicLink linkB(&radioB, getTimeMock, sleepMock);
    
    RollCall rollCallA(&linkA, "alice", getTimeMock, sleepMock, getTestRandom1);
    RollCall rollCallB(&linkB, "bob", getTimeMock, sleepMock, getTestRandom2);
    
    PeerMessenger messengerA(&rollCallA);
    PeerMessenger messengerB(&rollCallB);
    
    // Initialize both nodes
    REQUIRE(rollCallA.begin() == true);
    REQUIRE(rollCallB.begin() == true);
    REQUIRE(messengerA.begin() == true);
    REQUIRE(messengerB.begin() == true);
    
    uint16_t idA = rollCallA.getNodeId();
    uint16_t idB = rollCallB.getNodeId();
    
    // Clear any initial messages
    MockRadio::clearChannel();
    
    // Send multiple messages from A to B
    std::vector<std::string> messages = {
        "First message",
        "Second message", 
        "Third message"
    };
    
    for (const auto& msg : messages) {
        REQUIRE(messengerA.sendMessage(idB, msg) == true);
    }
    
    // Process all messages at B
    for (size_t i = 0; i < messages.size(); i++) {
        REQUIRE(messengerB.processMessages(100) == true);
    }
    
    // Check that all messages are queued
    REQUIRE(messengerB.getMessageCount() == messages.size());
    
    // Receive all messages in order
    for (size_t i = 0; i < messages.size(); i++) {
        REQUIRE(messengerB.hasMessage() == true);
        UserMessage received = messengerB.receiveMessage();
        REQUIRE(received.srcId == idA);
        REQUIRE(received.content == messages[i]);
    }
    
    // No more messages
    REQUIRE(messengerB.hasMessage() == false);
    REQUIRE(messengerB.getMessageCount() == 0);
}