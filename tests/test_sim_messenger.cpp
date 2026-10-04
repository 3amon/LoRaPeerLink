// PeerMessenger end to end: named nodes exchanging messages on a simulated
// half-duplex LoRa channel.
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "LoraBackoffLink.h"
#include "LoraBasicLink.h"
#include "PeerMessenger.h"
#include "RollCall.h"
#include "sim/SimHelpers.h"
#include "sim/SimNode.h"
#include "sim/SimRadio.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

using sim::RxMode;
using namespace simtest;

namespace {
const char* modeName(RxMode m) { return m == RxMode::Windowed ? "windowed radio" : "continuous radio"; }
}

TEMPLATE_TEST_CASE("Messenger: an acknowledged message sent by name arrives exactly once", "[simmessenger]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world(3);
    Node<TestType> alice(world, "alice", mode);
    Node<TestType> bob(world, "bob", mode);
    bool delivered = false;

    world.spawn([&] {
        alice.rollCall.begin();
        alice.pump(6000);
        delivered = alice.messenger.sendMessage("bob", "hello bob", true, 3000);
        alice.pump(10000);
    });
    world.spawn([&] {
        bob.rollCall.begin();
        bob.pump(12000);
    });
    REQUIRE(world.run(60000));
    REQUIRE(world.errors().empty());
    CHECK(delivered);
    REQUIRE(bob.inbox.size() == 1);
    CHECK(bob.inbox[0].content == "hello bob");
    CHECK(bob.inbox[0].srcId == alice.rollCall.getNodeId());
    CHECK(bob.inbox[0].srcName == "alice");
    CHECK(alice.inbox.empty());
}

TEMPLATE_TEST_CASE("Messenger: sending by name resolves a node that has not announced itself yet", "[simmessenger]", LoRaBasicLink, LoRaBackoffLink) {
    sim::World world(4);
    Node<TestType> alice(world, "alice", RxMode::Windowed);
    Node<TestType> bob(world, "bob", RxMode::Windowed);
    // Alice never hears Bob's announcements, so she has to ask.
    world.setDeliveryFilter([](const sim::Transmission& tx, int rxIndex, std::vector<uint8_t>&) {
        const std::string payload = tx.bytes.size() > 9 ? std::string(tx.bytes.begin() + 7, tx.bytes.end() - 2) : "";
        return !(tx.src == 1 && rxIndex == 0 && payload.rfind("HELLOIAM|", 0) == 0);
    });
    bool delivered = false;

    world.spawn([&] {
        alice.rollCall.begin();
        alice.pump(5000, 3000);
        delivered = alice.messenger.sendMessage("bob", "found you", true, 3000);
    });
    world.spawn([&] {
        bob.rollCall.begin();
        bob.pump(15000, 3000);
    });
    REQUIRE(world.run(60000));
    REQUIRE(world.errors().empty());
    CHECK(delivered);
    CHECK(bob.countContent("found you") == 1);
}

TEMPLATE_TEST_CASE("Messenger: messages that arrive while a name is being resolved are kept", "[simmessenger][loss]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world(5);
    Node<TestType> alice(world, "alice", mode);
    Node<TestType> bob(world, "bob", mode);
    bool resolved = true;
    int acked = 0;

    world.spawn([&] {
        alice.rollCall.begin();
        alice.pump(6000);
        // Nobody is called "ghost": this blocks for the full timeout while
        // RollCall, not PeerMessenger, is doing the listening.
        resolved = alice.messenger.sendMessage("ghost", "anyone?", false, 4000);
        alice.drain();
    });
    world.spawn([&] {
        bob.rollCall.begin();
        bob.pump(6500);
        for (int i = 0; i < 3; ++i) {
            if (bob.messenger.sendMessage("alice", "note " + std::to_string(i), true)) ++acked;
            bob.pump(sim::nowMs() + 300);
        }
        bob.pump(12000);
    });
    REQUIRE(world.run(60000));
    REQUIRE(world.errors().empty());
    CHECK_FALSE(resolved);
    CHECK(acked == 3);
    REQUIRE(alice.inbox.size() == 3);
    for (int i = 0; i < 3; ++i) CHECK(alice.inbox[i].content == "note " + std::to_string(i));
}

TEMPLATE_TEST_CASE("Messenger: calling RollCall and PeerMessenger alternately loses nothing", "[simmessenger][loss]", LoRaBasicLink, LoRaBackoffLink) {
    // This is how the WirelessStick application drives the stack. Version 1
    // dropped every user message that arrived during RollCall's half.
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world(6);
    Node<TestType> alice(world, "alice", mode);
    Node<TestType> bob(world, "bob", mode);
    const int kCount = 20;
    int acked = 0;
    bool done = false;

    world.spawn([&] {
        alice.rollCall.begin();
        while (!done) {
            alice.rollCall.processMessages(500);
            alice.messenger.processMessages(500);
            alice.drain();
        }
    });
    world.spawn([&] {
        bob.rollCall.begin();
        bob.pump(6000);
        for (int i = 0; i < kCount; ++i) {
            if (bob.messenger.sendMessage("alice", "m" + std::to_string(i), true)) ++acked;
            bob.pump(sim::nowMs() + 150 + sim::rand16() % 500);
        }
        done = true;
    });
    REQUIRE(world.run(300000));
    REQUIRE(world.errors().empty());
    CHECK(acked == kCount);
    REQUIRE(alice.inbox.size() == static_cast<size_t>(kCount));
    for (int i = 0; i < kCount; ++i) CHECK(alice.inbox[i].content == "m" + std::to_string(i));
}

TEMPLATE_TEST_CASE("Messenger: a broadcast reaches every node", "[simmessenger]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    sim::World world(7);
    std::vector<std::unique_ptr<Node<TestType>>> nodes;
    for (const char* name : {"n0", "n1", "n2", "n3"}) nodes.emplace_back(new Node<TestType>(world, name, mode));
    bool sent = false;

    world.spawn([&] {
        nodes[0]->rollCall.begin();
        nodes[0]->pump(8000, 4000);
        sent = nodes[0]->messenger.broadcastMessage("all hands");
        nodes[0]->pump(12000, 4000);
    });
    for (int i = 1; i < 4; ++i) {
        world.spawn([&, i] {
            nodes[i]->rollCall.begin();
            nodes[i]->pump(12000, 4000);
        });
    }
    REQUIRE(world.run(60000));
    REQUIRE(world.errors().empty());
    CHECK(sent);
    CHECK(nodes[0]->inbox.empty());
    for (int i = 1; i < 4; ++i) {
        INFO("node " << i);
        CHECK(nodes[i]->countContent("all hands") == 1);
    }
}

TEMPLATE_TEST_CASE("Messenger: an over-long message is refused, the longest allowed one arrives intact", "[simmessenger][bounds]", LoRaBasicLink, LoRaBackoffLink) {
    sim::World world(8);
    Node<TestType> alice(world, "alice", RxMode::Continuous);
    Node<TestType> bob(world, "bob", RxMode::Continuous);
    bool tooLongById = true, tooLongByName = true, tooLongBroadcast = true, maxOk = false;
    int txBefore = 0, txAfter = 0;
    std::string longest;
    size_t limit = 0;

    world.spawn([&] {
        alice.rollCall.begin();
        alice.pump(6000);
        limit = alice.messenger.maxMessageLength();
        const std::string over(limit + 1, 'x');
        const std::string wayOver(300, 'y');           // length does not fit in 8 bits
        txBefore = alice.radio.txCount;
        tooLongById = alice.messenger.sendMessage(bob.rollCall.getNodeId(), over, false);
        tooLongByName = alice.messenger.sendMessage("bob", wayOver, true);
        tooLongBroadcast = alice.messenger.broadcastMessage(wayOver);
        txAfter = alice.radio.txCount;

        longest.assign(limit, 'z');
        longest[0] = 'A';
        longest[limit - 1] = 'Z';
        maxOk = alice.messenger.sendMessage("bob", longest, true);
    });
    world.spawn([&] {
        bob.rollCall.begin();
        bob.pump(15000);
    });
    REQUIRE(world.run(60000));
    REQUIRE(world.errors().empty());
    CHECK(limit == 242);                               // 246 byte link payload minus "MSG|"
    CHECK_FALSE(tooLongById);
    CHECK_FALSE(tooLongByName);
    CHECK_FALSE(tooLongBroadcast);
    CHECK(txAfter == txBefore);                        // nothing was transmitted for them
    CHECK(maxOk);
    REQUIRE(bob.inbox.size() == 1);
    CHECK(bob.inbox[0].content == longest);
}

TEST_CASE("Messenger: message content is binary safe", "[simmessenger]") {
    sim::World world(9);
    Node<LoRaBackoffLink> alice(world, "alice", RxMode::Continuous);
    Node<LoRaBackoffLink> bob(world, "bob", RxMode::Continuous);
    std::string blob;
    for (int i = 0; i < 200; ++i) blob.push_back(static_cast<char>(i * 7));   // includes NUL and '|'
    bool ok = false;

    world.spawn([&] {
        alice.rollCall.begin();
        alice.pump(6000);
        ok = alice.messenger.sendMessage("bob", blob, true);
        // A message that looks like a RollCall message is still user data.
        alice.messenger.sendMessage("bob", "HELLOIAM|evil AT 5", true);
    });
    world.spawn([&] { bob.rollCall.begin(); bob.pump(15000); });
    REQUIRE(world.run(60000));
    CHECK(ok);
    REQUIRE(bob.inbox.size() == 2);
    CHECK(bob.inbox[0].content == blob);
    CHECK(bob.inbox[1].content == "HELLOIAM|evil AT 5");
    CHECK(bob.rollCall.getNameToIdMap().count("evil") == 0);
}

TEST_CASE("Messenger: the receive queue is bounded and keeps the newest messages", "[simmessenger][bounds]") {
    sim::World world(10);
    Node<LoRaBackoffLink> alice(world, "alice", RxMode::Continuous);
    Node<LoRaBackoffLink> bob(world, "bob", RxMode::Continuous);
    const int kCount = PEER_MESSENGER_MAX_QUEUE + 5;
    bool done = false;

    world.spawn([&] {
        alice.rollCall.begin();
        alice.pump(6000);
        for (int i = 0; i < kCount; ++i) alice.messenger.sendMessage("bob", "q" + std::to_string(i), true);
        done = true;
    });
    world.spawn([&] {
        bob.rollCall.begin();
        while (!done) bob.messenger.processMessages(500);      // never reads the queue
        bob.messenger.processMessages(500);
    });
    REQUIRE(world.run(120000));
    CHECK(bob.messenger.getMessageCount() == PEER_MESSENGER_MAX_QUEUE);
    CHECK(bob.messenger.droppedMessages() == 5);
    CHECK(bob.messenger.receiveMessage().content == "q5");      // q0..q4 were pushed out
}

TEST_CASE("Messenger: receiving from an empty queue is harmless", "[simmessenger]") {
    sim::World world;
    Node<LoRaBasicLink> node(world, "solo", RxMode::Windowed);
    CHECK_FALSE(node.messenger.hasMessage());
    UserMessage m = node.messenger.receiveMessage();
    CHECK(m.srcId == 0);
    CHECK(m.content.empty());
    CHECK(m.srcName.empty());
}

TEST_CASE("Messenger: destroying a messenger detaches it from RollCall", "[simmessenger]") {
    sim::World world;
    auto& radio = world.addRadio();
    LoRaBasicLink link(&radio, sim::nowMs, sim::sleepMs);
    RollCall rollCall(&link, "node", sim::nowMs, sim::sleepMs, sim::rand16);
    {
        PeerMessenger messenger(&rollCall);
        CHECK(rollCall.dataHandlerContext() == &messenger);
    }
    CHECK(rollCall.dataHandlerContext() == nullptr);
    PeerMessenger orphan(nullptr);
    CHECK_FALSE(orphan.begin());
    CHECK_FALSE(orphan.sendMessage(1, "x"));
    CHECK_FALSE(orphan.processMessages(0));
    CHECK(orphan.maxMessageLength() == 0);
}

TEMPLATE_TEST_CASE("Messenger soak: four nodes, lossy channel, every acknowledged message arrives exactly once", "[simmessenger][soak]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    uint64_t seed = GENERATE(101, 102, 103);
    INFO(modeName(mode) << ", seed " << seed);
    const int kNodes = 4;
    const int kMessages = 15;
    sim::World world(seed);
    world.setLossProbability(0.05);
    std::vector<std::unique_ptr<Node<TestType>>> nodes;
    for (int i = 0; i < kNodes; ++i) nodes.emplace_back(new Node<TestType>(world, "node" + std::to_string(i), mode));
    std::vector<std::string> ackedTexts[kNodes];   // indexed by destination
    int attempted = 0;

    for (int i = 0; i < kNodes; ++i) {
        world.spawn([&, i] {
            Node<TestType>& self = *nodes[i];
            self.rollCall.begin();
            // Let everybody learn everybody, then start at slightly different
            // times (the synchronised case has its own test in test_sim_link.cpp).
            self.pump(40000 + sim::rand16() % 8000, 3000);
            for (int m = 0; m < kMessages; ++m) {
                int dest = (i + 1 + sim::rand16() % (kNodes - 1)) % kNodes;
                std::string text = "from" + std::to_string(i) + "#" + std::to_string(m);
                ++attempted;
                if (self.messenger.sendMessage("node" + std::to_string(dest), text, true, 3000)) {
                    ackedTexts[dest].push_back(text);
                }
                // About one message every twelve seconds per node, roughly 5% channel
                // load. An uncoordinated shared channel cannot carry much more: at 20%
                // load a third of all frames collide.
                self.pump(sim::nowMs() + 8000 + sim::rand16() % 8000, 3000);
            }
            self.pump(320000, 3000);
        }, static_cast<uint32_t>(i * 211));
    }
    REQUIRE(world.run(600000));
    REQUIRE(world.errors().empty());

    int ackedTotal = 0;
    for (int d = 0; d < kNodes; ++d) {
        std::map<std::string, int> seen;
        for (auto& m : nodes[d]->inbox) ++seen[m.content];
        for (auto& kv : seen) {
            INFO("node " << d << " message " << kv.first);
            CHECK(kv.second == 1);                 // never delivered twice
        }
        for (auto& text : ackedTexts[d]) {
            INFO("node " << d << " message " << text);
            CHECK(seen.count(text) == 1);          // acknowledged means delivered
        }
        ackedTotal += static_cast<int>(ackedTexts[d].size());
    }
    // With three transmissions per message and 5% loss nearly everything gets through.
    CHECK(attempted == kNodes * kMessages);
    CHECK(ackedTotal >= attempted * 95 / 100);
}
