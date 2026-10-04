// RollCall (naming and discovery) on a simulated half-duplex LoRa channel.
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

#include <memory>
#include <set>
#include <string>
#include <vector>

using sim::RxMode;
using namespace simtest;

namespace {

const char* modeName(RxMode m) { return m == RxMode::Windowed ? "windowed radio" : "continuous radio"; }

/** True if every node knows every other node's current name and ID, and nothing else. */
template <class NodeT>
std::string tableProblems(const std::vector<std::unique_ptr<NodeT>>& nodes) {
    std::string problems;
    std::set<uint16_t> ids;
    std::set<std::string> names;
    for (auto& n : nodes) {
        if (!ids.insert(n->rollCall.getNodeId()).second) problems += "duplicate ID " + std::to_string(n->rollCall.getNodeId()) + "; ";
        if (!names.insert(n->rollCall.getNodeName()).second) problems += "duplicate name " + n->rollCall.getNodeName() + "; ";
    }
    for (auto& n : nodes) {
        const auto& byName = n->rollCall.getNameToIdMap();
        const auto& byId = n->rollCall.getIdToNameMap();
        for (auto& other : nodes) {
            auto it = byName.find(other->rollCall.getNodeName());
            if (it == byName.end()) {
                problems += n->rollCall.getNodeName() + " does not know " + other->rollCall.getNodeName() + "; ";
            } else if (it->second != other->rollCall.getNodeId()) {
                problems += n->rollCall.getNodeName() + " has the wrong ID for " + other->rollCall.getNodeName() + "; ";
            }
        }
        if (byName.size() != nodes.size()) {
            problems += n->rollCall.getNodeName() + " has " + std::to_string(byName.size()) + " names, expected " + std::to_string(nodes.size()) + "; ";
        }
        if (byId.size() != byName.size()) {
            problems += n->rollCall.getNodeName() + " has inconsistent tables; ";
        }
    }
    return problems;
}

// Random source whose first value (the initial node ID) is fixed.
thread_local int t_fixedCalls = 0;
uint16_t firstIdIs4660() {
    return (t_fixedCalls++ == 0) ? static_cast<uint16_t>(0x1234) : sim::rand16();
}
// Same, and the second value (the nonce) is fixed too.
thread_local uint16_t t_nonce = 0;
uint16_t firstIdAndNonceFixed() {
    int call = t_fixedCalls++;
    if (call == 0) return 0x1234;
    if (call == 1) return t_nonce;
    return sim::rand16();
}

} // namespace

TEMPLATE_TEST_CASE("RollCall: two nodes switched on together find each other", "[simrollcall]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world(5);
    std::vector<std::unique_ptr<Node<TestType>>> nodes;
    nodes.emplace_back(new Node<TestType>(world, "alpha", mode));
    nodes.emplace_back(new Node<TestType>(world, "bravo", mode));
    bool began[2] = {false, false};

    for (int i = 0; i < 2; ++i) {
        world.spawn([&, i] {
            began[i] = nodes[i]->rollCall.begin();
            nodes[i]->pump(8000);
        });
    }
    REQUIRE(world.run(60000));
    REQUIRE(world.errors().empty());
    CHECK(began[0]);
    CHECK(began[1]);
    CHECK(tableProblems(nodes) == "");
}

TEMPLATE_TEST_CASE("RollCall: six nodes switched on at the same instant converge", "[simrollcall]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    uint64_t seed = GENERATE(1, 2, 3);
    INFO(modeName(mode) << ", seed " << seed);
    sim::World world(seed);
    std::vector<std::unique_ptr<Node<TestType>>> nodes;
    for (int i = 0; i < 6; ++i) nodes.emplace_back(new Node<TestType>(world, "node" + std::to_string(i), mode));

    // A windowed radio loses any frame that is on the air when a receive
    // window ends, so the application listens in long windows and the test
    // allows several announcement periods.
    for (auto& n : nodes) {
        Node<TestType>* node = n.get();
        world.spawn([node] {
            node->rollCall.begin();
            node->pump(240000, 5000);
        });
    }
    REQUIRE(world.run(500000));
    REQUIRE(world.errors().empty());
    CHECK(tableProblems(nodes) == "");
}

TEMPLATE_TEST_CASE("RollCall: nodes that all ask for the same name end up with unique names", "[simrollcall][collision]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    uint64_t seed = GENERATE(1, 2, 3);
    INFO(modeName(mode) << ", seed " << seed);
    sim::World world(seed);
    std::vector<std::unique_ptr<Node<TestType>>> nodes;
    for (int i = 0; i < 4; ++i) nodes.emplace_back(new Node<TestType>(world, "lora-simp", mode));

    for (auto& n : nodes) {
        Node<TestType>* node = n.get();
        world.spawn([node] {
            node->rollCall.begin();
            node->pump(240000, 5000);
        });
    }
    REQUIRE(world.run(500000));
    REQUIRE(world.errors().empty());
    CHECK(tableProblems(nodes) == "");

    // Exactly one node keeps the plain name: the one with the smallest ID.
    int keepers = 0;
    uint16_t smallestId = 0xFFFF;
    for (auto& n : nodes) smallestId = std::min(smallestId, n->rollCall.getNodeId());
    for (auto& n : nodes) {
        if (n->rollCall.getNodeName() == "lora-simp") {
            ++keepers;
            CHECK(n->rollCall.getNodeId() == smallestId);
        } else {
            CHECK(n->rollCall.getNodeName().rfind("lora-simp-", 0) == 0);
        }
    }
    CHECK(keepers == 1);
}

TEST_CASE("RollCall: two nodes with the same ID - exactly one moves", "[simrollcall][collision]") {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world(9);
    std::vector<std::unique_ptr<Node<LoRaBackoffLink>>> nodes;
    nodes.emplace_back(new Node<LoRaBackoffLink>(world, "apple", mode, firstIdIs4660));
    nodes.emplace_back(new Node<LoRaBackoffLink>(world, "banana", mode, firstIdIs4660));
    uint16_t initial[2] = {0, 0};

    for (int i = 0; i < 2; ++i) {
        world.spawn([&, i] {
            t_fixedCalls = 0;
            nodes[i]->rollCall.begin();
            initial[i] = 0x1234;
            nodes[i]->pump(60000);
        });
    }
    REQUIRE(world.run(200000));
    REQUIRE(world.errors().empty());
    CHECK(tableProblems(nodes) == "");
    // "apple" sorts first and keeps the ID; "banana" moves.
    CHECK(nodes[0]->rollCall.getNodeId() == 0x1234);
    CHECK(nodes[1]->rollCall.getNodeId() != 0x1234);
    CHECK(nodes[0]->rollCall.getNodeName() == "apple");
    CHECK(nodes[1]->rollCall.getNodeName() == "banana");
}

TEST_CASE("RollCall: two nodes with the same name AND the same ID are told apart by their nonce", "[simrollcall][collision]") {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world(13);
    std::vector<std::unique_ptr<Node<LoRaBackoffLink>>> nodes;
    nodes.emplace_back(new Node<LoRaBackoffLink>(world, "twin", mode, firstIdAndNonceFixed));
    nodes.emplace_back(new Node<LoRaBackoffLink>(world, "twin", mode, firstIdAndNonceFixed));

    for (int i = 0; i < 2; ++i) {
        world.spawn([&, i] {
            t_fixedCalls = 0;
            t_nonce = static_cast<uint16_t>(100 + i);     // node 1 has the larger nonce
            nodes[i]->rollCall.begin();
            nodes[i]->pump(90000);
        });
    }
    REQUIRE(world.run(200000));
    REQUIRE(world.errors().empty());
    CHECK(tableProblems(nodes) == "");
    CHECK(nodes[0]->rollCall.getNodeName() == "twin");
    CHECK(nodes[0]->rollCall.getNodeId() == 0x1234);
    CHECK(nodes[1]->rollCall.getNodeName() != "twin");
    CHECK(nodes[1]->rollCall.getNodeId() != 0x1234);
}

TEMPLATE_TEST_CASE("RollCall: whoIs asks on the air for a name it has not heard", "[simrollcall][query]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world(21);
    std::vector<std::unique_ptr<Node<TestType>>> nodes;
    nodes.emplace_back(new Node<TestType>(world, "asker", mode));
    nodes.emplace_back(new Node<TestType>(world, "target", mode));

    // The asker never hears any of the target's announcements.
    world.setDeliveryFilter([](const sim::Transmission& tx, int rxIndex, std::vector<uint8_t>& bytes) {
        (void)bytes;
        const std::string payload = tx.bytes.size() > 9 ? std::string(tx.bytes.begin() + 7, tx.bytes.end() - 2) : "";
        return !(tx.src == 1 && rxIndex == 0 && payload.rfind("HELLOIAM|", 0) == 0);
    });

    uint16_t resolved = 0;
    std::string reverse;
    bool knewBefore = true;
    world.spawn([&] {
        nodes[0]->rollCall.begin();
        nodes[0]->pump(5000);
        knewBefore = nodes[0]->rollCall.getNameToIdMap().count("target") > 0;
        resolved = nodes[0]->rollCall.whoIs("target", 2000);
        nodes[0]->pump(8000);
    });
    world.spawn([&] {
        nodes[1]->rollCall.begin();
        nodes[1]->pump(12000);
        reverse = nodes[1]->rollCall.whereIs(nodes[0]->rollCall.getNodeId(), 10);   // already cached
    });
    REQUIRE(world.run(60000));
    REQUIRE(world.errors().empty());
    CHECK_FALSE(knewBefore);
    CHECK(resolved != 0);
    CHECK(resolved == nodes[1]->rollCall.getNodeId());
    CHECK(reverse == "asker");
}

TEMPLATE_TEST_CASE("RollCall: whereIs asks on the air for an ID it has not heard", "[simrollcall][query]", LoRaBasicLink, LoRaBackoffLink) {
    sim::World world(22);
    std::vector<std::unique_ptr<Node<TestType>>> nodes;
    nodes.emplace_back(new Node<TestType>(world, "asker", RxMode::Windowed));
    nodes.emplace_back(new Node<TestType>(world, "target", RxMode::Windowed));
    world.setDeliveryFilter([](const sim::Transmission& tx, int rxIndex, std::vector<uint8_t>&) {
        const std::string payload = tx.bytes.size() > 9 ? std::string(tx.bytes.begin() + 7, tx.bytes.end() - 2) : "";
        return !(tx.src == 1 && rxIndex == 0 && payload.rfind("HELLOIAM|", 0) == 0);
    });

    std::string name;
    world.spawn([&] {
        nodes[0]->rollCall.begin();
        nodes[0]->pump(5000);
        name = nodes[0]->rollCall.whereIs(nodes[1]->rollCall.getNodeId(), 2000);
    });
    world.spawn([&] {
        nodes[1]->rollCall.begin();
        nodes[1]->pump(12000);
    });
    REQUIRE(world.run(60000));
    REQUIRE(world.errors().empty());
    CHECK(name == "target");
}

TEST_CASE("RollCall: queries for something that does not exist give up after the timeout", "[simrollcall][query]") {
    sim::World world;
    Node<LoRaBackoffLink> node(world, "lonely", RxMode::Windowed);
    uint16_t id = 1;
    std::string name = "x";
    uint32_t tWho = 0, tWhere = 0;
    world.spawn([&] {
        node.rollCall.begin();
        uint32_t t0 = sim::nowMs();
        id = node.rollCall.whoIs("nobody", 1500);
        tWho = sim::nowMs() - t0;
        t0 = sim::nowMs();
        name = node.rollCall.whereIs(4242, 1500);
        tWhere = sim::nowMs() - t0;
    });
    REQUIRE(world.run(60000));
    CHECK(id == 0);
    CHECK(name == "");
    CHECK(tWho >= 1500);
    CHECK(tWho < 1800);
    CHECK(tWhere >= 1500);
    CHECK(tWhere < 1800);
    // Each query is sent three times, spread over the timeout.
    int whois = 0, whereis = 0;
    for (auto& tx : world.transmissions()) {
        const std::string payload(tx.bytes.begin() + 7, tx.bytes.end() - 2);
        if (payload == "WHOIS|nobody") ++whois;
        if (payload == "WHEREIS|4242") ++whereis;
    }
    CHECK(whois == static_cast<int>(RollCall::QUERY_SENDS));
    CHECK(whereis == static_cast<int>(RollCall::QUERY_SENDS));
}

TEST_CASE("RollCall: malformed messages from the air never crash a node or pollute its table", "[simrollcall][fuzz]") {
    sim::World world(31);
    Node<LoRaBasicLink> node(world, "victim", RxMode::Continuous);
    auto& attacker = world.addRadio(RxMode::Continuous);

    const std::string longName(200, 'x');
    const std::vector<std::string> junk = {
        "HELLOIAM|", "HELLOIAM|x", "HELLOIAM|x AT ", "HELLOIAM|x AT abc", "HELLOIAM|x AT 12abc",
        "HELLOIAM|x AT 99999999999999999999999", "HELLOIAM|x AT 0", "HELLOIAM|x AT 65535",
        "HELLOIAM|x AT 70000", "HELLOIAM|x AT -5", "HELLOIAM|x AT +5", "HELLOIAM|x AT 5 ",
        "HELLOIAM| AT 5", "HELLOIAM|x AT 5 #", "HELLOIAM|x AT 5 #abc", "HELLOIAM|x AT 5 #99999999",
        "HELLOIAM|" + longName + " AT 5", std::string("HELLOIAM|a\0b AT 5", 17), "HELLOIAM|a|b AT 5",
        "HELLOIAM|bad\nname AT 5",
        "WHOIS|", "WHOIS|" + longName,
        "WHEREIS|", "WHEREIS|zzz", "WHEREIS|-1", "WHEREIS|99999999999999999999", "WHEREIS|0",
        "RESP|", "RESP|x", "RESP|x AT ", "RESP|x AT abc", "RESP| AT 7", "RESP|x AT 4294967301",
        // Claims on the victim's own identity in a response must be ignored.
        "RESP|victim AT 7",
    };

    bool alive = false;
    world.spawn([&] {
        node.rollCall.begin();
        node.pump(60000, 1000);
        alive = true;
    });
    world.spawn([&] {
        sim::sleepMs(3000);
        uint8_t seq = 0;
        for (const auto& text : junk) {
            auto f = frame(0xFFFF, 0x0666, seq++, 0, text);
            attacker.send(f.data(), f.size());
            sim::sleepMs(30);
        }
        // "RESP|<name> AT <victim's id>" for a different name must be ignored too.
        auto f = frame(0xFFFF, 0x0666, seq++, 0, "RESP|intruder AT " + std::to_string(node.rollCall.getNodeId()));
        attacker.send(f.data(), f.size());
        sim::sleepMs(30);
        // A well formed announcement afterwards still works.
        f = frame(0xFFFF, 0x0666, seq++, 0, "HELLOIAM|friend AT 1638");
        attacker.send(f.data(), f.size());
    });
    REQUIRE(world.run(120000));
    REQUIRE(world.errors().empty());
    CHECK(alive);
    const auto& byName = node.rollCall.getNameToIdMap();
    CHECK(byName.size() == 2);
    CHECK(byName.count("victim") == 1);
    REQUIRE(byName.count("friend") == 1);
    CHECK(byName.at("friend") == 1638);
    CHECK(node.rollCall.getNodeName() == "victim");
    CHECK(node.rollCall.getIdToNameMap().size() == 2);
}

TEST_CASE("RollCall: random text never crashes the parser", "[simrollcall][fuzz]") {
    // No radio needed: feed the message handler directly.
    sim::World world(77);
    Node<LoRaBasicLink> node(world, "victim", RxMode::Continuous);
    int handled = 0;
    world.spawn([&] {
        node.rollCall.begin();
        std::mt19937 rng(4321);
        const char* prefixes[] = {"HELLOIAM|", "WHOIS|", "WHEREIS|", "RESP|"};
        const char alphabet[] = "abAT 0123456789#|-+\n\0xyz";
        for (int i = 0; i < 20000; ++i) {
            std::string msg = prefixes[rng() % 4];
            size_t len = rng() % 40;
            for (size_t k = 0; k < len; ++k) msg.push_back(alphabet[rng() % (sizeof(alphabet) - 1)]);
            if (rng() % 4 == 0) msg += " AT " + std::to_string(rng() % 70000);
            if (node.rollCall.processRollCallMessage(msg, static_cast<uint16_t>(rng()))) ++handled;
        }
    });
    REQUIRE(world.run(10000000));
    REQUIRE(world.errors().empty());
    CHECK(handled > 0);
    CHECK(node.rollCall.getNameToIdMap().size() <= ROLLCALL_MAX_PEERS);
    CHECK(node.rollCall.getNameToIdMap().size() == node.rollCall.getIdToNameMap().size());
    // Its own entry survived whatever was thrown at it.
    REQUIRE(node.rollCall.getNameToIdMap().count(node.rollCall.getNodeName()) == 1);
    CHECK(node.rollCall.getNameToIdMap().at(node.rollCall.getNodeName()) == node.rollCall.getNodeId());
}

TEST_CASE("RollCall: a name containing ' AT ' works", "[simrollcall]") {
    sim::World world(41);
    std::vector<std::unique_ptr<Node<LoRaBackoffLink>>> nodes;
    nodes.emplace_back(new Node<LoRaBackoffLink>(world, "CAT AT HOME", RxMode::Windowed));
    nodes.emplace_back(new Node<LoRaBackoffLink>(world, "dog", RxMode::Windowed));
    uint16_t resolved = 0;
    world.spawn([&] { nodes[0]->rollCall.begin(); nodes[0]->pump(10000); });
    world.spawn([&] {
        nodes[1]->rollCall.begin();
        nodes[1]->pump(6000);
        resolved = nodes[1]->rollCall.whoIs("CAT AT HOME", 2000);
    });
    REQUIRE(world.run(60000));
    REQUIRE(world.errors().empty());
    CHECK(resolved == nodes[0]->rollCall.getNodeId());
    CHECK(tableProblems(nodes) == "");
}

TEST_CASE("RollCall: invalid names are refused at begin()", "[simrollcall]") {
    std::string bad = GENERATE(std::string(""), std::string(ROLLCALL_MAX_NAME_LEN + 1, 'n'), std::string("a|b"), std::string("tab\there"));
    sim::World world;
    Node<LoRaBasicLink> node(world, bad, RxMode::Windowed);
    bool ok = true;
    world.spawn([&] { ok = node.rollCall.begin(); });
    REQUIRE(world.run(10000));
    CHECK_FALSE(ok);
    CHECK(node.radio.txCount == 0);
    CHECK(RollCall::isValidName(std::string(ROLLCALL_MAX_NAME_LEN, 'n')));
}

TEST_CASE("RollCall: announcements keep going out when the application only calls PeerMessenger", "[simrollcall][periodic]") {
    sim::World world(51);
    Node<LoRaBackoffLink> node(world, "beacon", RxMode::Windowed);
    world.spawn([&] {
        node.rollCall.begin();
        node.pump(125000);          // only PeerMessenger::processMessages()
    });
    REQUIRE(world.run(300000));
    std::vector<uint32_t> times;
    for (auto& tx : world.transmissions()) {
        const std::string payload(tx.bytes.begin() + 7, tx.bytes.end() - 2);
        if (payload.rfind("HELLOIAM|beacon AT ", 0) == 0) times.push_back(static_cast<uint32_t>(tx.startUs / 1000));
    }
    // At begin (after up to 0.5 s of random delay), once more 1-4 s after the
    // collision listening period, then roughly every 30 s (27-33 s).
    REQUIRE(times.size() >= 5);
    CHECK(times[0] < 700);
    CHECK(times[1] - times[0] > 2000);
    CHECK(times[1] - times[0] < 5500);
    for (size_t i = 2; i < times.size(); ++i) {
        CHECK(times[i] - times[i - 1] >= 27000);
        CHECK(times[i] - times[i - 1] <= 33500);
    }
}

TEST_CASE("RollCall: a node that joins later is known to everybody within seconds", "[simrollcall]") {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world(61);
    std::vector<std::unique_ptr<Node<LoRaBackoffLink>>> nodes;
    for (const char* name : {"early1", "early2", "late"}) nodes.emplace_back(new Node<LoRaBackoffLink>(world, name, mode));
    std::string atJoinPlus5s;

    world.spawn([&] { nodes[0]->rollCall.begin(); nodes[0]->pump(40000); });
    world.spawn([&] { nodes[1]->rollCall.begin(); nodes[1]->pump(40000); });
    world.spawn([&] {
        nodes[2]->rollCall.begin();
        nodes[2]->pump(sim::nowMs() + 5000);
        atJoinPlus5s = tableProblems(nodes);     // well before the next 30 s announcement
        nodes[2]->pump(40000);
    }, 12000);
    REQUIRE(world.run(100000));
    REQUIRE(world.errors().empty());
    CHECK(atJoinPlus5s == "");
}

TEST_CASE("RollCall: a node that restarts with a new ID replaces its old table entry", "[simrollcall]") {
    sim::World world(71);
    Node<LoRaBackoffLink> stable(world, "stable", RxMode::Continuous);
    auto& radio = world.addRadio(RxMode::Continuous);
    LoRaBackoffLink link(&radio, sim::nowMs, sim::sleepMs);
    uint16_t firstId = 0, secondId = 0;

    world.spawn([&] { stable.rollCall.begin(); stable.pump(30000); });
    world.spawn([&] {
        {
            RollCall first(&link, "phoenix", sim::nowMs, sim::sleepMs, sim::rand16);
            first.begin();
            firstId = first.getNodeId();
            for (int i = 0; i < 10; ++i) first.processMessages(500);
        }
        RollCall second(&link, "phoenix", sim::nowMs, sim::sleepMs, sim::rand16);   // "reboot"
        second.begin();
        secondId = second.getNodeId();
        while (sim::nowMs() < 25000) second.processMessages(500);
    });
    REQUIRE(world.run(100000));
    REQUIRE(world.errors().empty());
    REQUIRE(firstId != secondId);
    const auto& byName = stable.rollCall.getNameToIdMap();
    const auto& byId = stable.rollCall.getIdToNameMap();
    REQUIRE(byName.count("phoenix") == 1);
    CHECK(byName.at("phoenix") == secondId);
    CHECK(byId.count(firstId) == 0);
    CHECK(byId.size() == 2);
}

TEST_CASE("RollCall: the peer table is bounded", "[simrollcall][bounds]") {
    sim::World world(81);
    Node<LoRaBasicLink> node(world, "full", RxMode::Continuous);
    world.spawn([&] {
        node.rollCall.begin();
        for (int i = 0; i < 500; ++i) {
            node.rollCall.processRollCallMessage("HELLOIAM|peer" + std::to_string(i) + " AT " + std::to_string(100 + i), static_cast<uint16_t>(100 + i));
        }
    });
    REQUIRE(world.run(1000000));
    CHECK(node.rollCall.getNameToIdMap().size() == ROLLCALL_MAX_PEERS);
    CHECK(node.rollCall.getIdToNameMap().size() == ROLLCALL_MAX_PEERS);
    CHECK(node.rollCall.getNameToIdMap().count("full") == 1);
}

TEST_CASE("RollCall: ID parsing accepts exactly the valid range", "[simrollcall][parse]") {
    uint16_t id = 0;
    CHECK(RollCall::parseNodeId("1", id));
    CHECK(id == 1);
    CHECK(RollCall::parseNodeId("65534", id));
    CHECK(id == 65534);
    CHECK(RollCall::parseNodeId("00042", id));
    CHECK(id == 42);
    for (const char* bad : {"", "0", "65535", "65536", "99999", "100000", "12a", "a12", " 12", "12 ", "-1", "+1", "1.5", "0x10"}) {
        INFO("'" << bad << "'");
        CHECK_FALSE(RollCall::parseNodeId(bad, id));
    }
}
