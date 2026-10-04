// Seed sweeps. Each scenario is run with many different random seeds and the
// success rate is checked, which says more about a lossy radio protocol than
// any single run.
//
// These are hidden from the default run (tag starts with a dot). Run them with
//     ./build/tests/test_all "[.stress]"
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "LoraBackoffLink.h"
#include "LoraBasicLink.h"
#include "sim/SimHelpers.h"
#include "sim/SimNode.h"
#include "sim/SimRadio.h"

#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <typeinfo>
#include <vector>

using sim::RxMode;
using namespace simtest;

namespace {

const int kSeeds = 200;

const char* modeName(RxMode m) { return m == RxMode::Windowed ? "windowed" : "continuous"; }

template <class NodeT>
bool converged(const std::vector<std::unique_ptr<NodeT>>& nodes) {
    std::set<uint16_t> ids;
    std::set<std::string> names;
    for (auto& n : nodes) {
        if (!ids.insert(n->rollCall.getNodeId()).second) return false;
        if (!names.insert(n->rollCall.getNodeName()).second) return false;
    }
    for (auto& n : nodes) {
        const auto& byName = n->rollCall.getNameToIdMap();
        if (byName.size() != nodes.size()) return false;
        if (n->rollCall.getIdToNameMap().size() != nodes.size()) return false;
        for (auto& other : nodes) {
            auto it = byName.find(other->rollCall.getNodeName());
            if (it == byName.end() || it->second != other->rollCall.getNodeId()) return false;
        }
    }
    return true;
}

void report(const char* scenario, const char* link, RxMode mode, int ok, int total) {
    std::printf("  %-34s %-8s %-10s %3d / %3d  (%.1f%%)\n", scenario, link, modeName(mode), ok, total, 100.0 * ok / total);
}

} // namespace

TEMPLATE_TEST_CASE("Stress: acknowledged message by name between two nodes", "[.stress]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    int ok = 0, unfinished = 0, errors = 0, duplicates = 0;
    for (int seed = 1; seed <= kSeeds; ++seed) {
        sim::World world(static_cast<uint64_t>(seed));
        Node<TestType> alice(world, "alice", mode);
        Node<TestType> bob(world, "bob", mode);
        bool delivered = false;
        world.spawn([&] {
            alice.rollCall.begin();
            alice.pump(6000 + sim::rand16() % 3000, 3000);
            delivered = alice.messenger.sendMessage("bob", "ping", true, 3000);
            alice.pump(20000, 3000);
        });
        world.spawn([&] { bob.rollCall.begin(); bob.pump(20000, 3000); }, static_cast<uint32_t>(seed % 700));
        if (!world.run(120000)) ++unfinished;
        if (!world.errors().empty()) ++errors;
        if (bob.countContent("ping") > 1) ++duplicates;
        if (delivered && bob.countContent("ping") == 1) ++ok;
    }
    report("message by name with ACK", std::string(typeid(TestType).name()).find("Backoff") != std::string::npos ? "backoff" : "basic", mode, ok, kSeeds);
    CHECK(unfinished == 0);
    CHECK(errors == 0);
    CHECK(duplicates == 0);
    CHECK(ok >= kSeeds * 97 / 100);
}

TEMPLATE_TEST_CASE("Stress: five nodes started together learn about each other", "[.stress]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    int ok = 0, unfinished = 0, errors = 0;
    for (int seed = 1; seed <= kSeeds; ++seed) {
        sim::World world(static_cast<uint64_t>(seed));
        std::vector<std::unique_ptr<Node<TestType>>> nodes;
        for (int i = 0; i < 5; ++i) nodes.emplace_back(new Node<TestType>(world, "node" + std::to_string(i), mode));
        for (auto& n : nodes) {
            Node<TestType>* node = n.get();
            world.spawn([node] { node->rollCall.begin(); node->pump(180000, 5000); });
        }
        if (!world.run(400000)) ++unfinished;
        if (!world.errors().empty()) ++errors;
        if (converged(nodes)) ++ok;
    }
    report("5 nodes converge in 3 minutes", std::string(typeid(TestType).name()).find("Backoff") != std::string::npos ? "backoff" : "basic", mode, ok, kSeeds);
    CHECK(unfinished == 0);
    CHECK(errors == 0);
    CHECK(ok >= kSeeds * 99 / 100);
}

TEMPLATE_TEST_CASE("Stress: four nodes asking for the same name end up with unique names", "[.stress]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    int ok = 0, unfinished = 0, errors = 0, wrongKeeper = 0;
    for (int seed = 1; seed <= kSeeds; ++seed) {
        sim::World world(static_cast<uint64_t>(seed));
        std::vector<std::unique_ptr<Node<TestType>>> nodes;
        for (int i = 0; i < 4; ++i) nodes.emplace_back(new Node<TestType>(world, "same", mode));
        for (size_t i = 0; i < nodes.size(); ++i) {
            Node<TestType>* node = nodes[i].get();
            world.spawn([node] { node->rollCall.begin(); node->pump(180000, 5000); }, static_cast<uint32_t>(i * 1300));
        }
        if (!world.run(400000)) ++unfinished;
        if (!world.errors().empty()) ++errors;
        if (converged(nodes)) ++ok;
        int keepers = 0;
        for (auto& n : nodes) if (n->rollCall.getNodeName() == "same") ++keepers;
        if (keepers != 1) ++wrongKeeper;
    }
    report("4 identical names resolve", std::string(typeid(TestType).name()).find("Backoff") != std::string::npos ? "backoff" : "basic", mode, ok, kSeeds);
    CHECK(unfinished == 0);
    CHECK(errors == 0);
    CHECK(ok >= kSeeds * 99 / 100);
    CHECK(wrongKeeper <= kSeeds / 100);
}

TEMPLATE_TEST_CASE("Stress: simultaneous acknowledged sends from four nodes", "[.stress]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    int ok = 0, unfinished = 0, duplicates = 0;
    for (int seed = 1; seed <= kSeeds; ++seed) {
        const int kSenders = 4;
        const uint16_t sink = 0x5151;
        sim::World world(static_cast<uint64_t>(seed));
        auto& rsink = world.addRadio(mode);
        TestType lsink(&rsink, sim::nowMs, sim::sleepMs);
        lsink.setLocalId(sink);
        std::vector<std::unique_ptr<TestType>> links;
        bool acked[kSenders] = {false, false, false, false};
        std::vector<Rx> got;
        int finished = 0;
        for (int i = 0; i < kSenders; ++i) {
            auto& r = world.addRadio(mode);
            links.emplace_back(new TestType(&r, sim::nowMs, sim::sleepMs));
            const uint16_t id = static_cast<uint16_t>(0x1100 + i);
            links[i]->setLocalId(id);
            world.spawn([&, i, id] {
                acked[i] = sendText(*links[i], id, sink, "burst" + std::to_string(i), true, 8);
                ++finished;
            }, 100);
        }
        world.spawn([&] { while (finished < kSenders) listenUntil(lsink, sim::nowMs() + 2000, got, 2000); });
        if (!world.run(300000)) ++unfinished;
        bool all = true;
        for (int i = 0; i < kSenders; ++i) {
            int n = countPayload(got, "burst" + std::to_string(i));
            if (n > 1) ++duplicates;
            if (!acked[i] || n != 1) all = false;
        }
        if (all) ++ok;
    }
    report("4 simultaneous senders, 8 tries", std::string(typeid(TestType).name()).find("Backoff") != std::string::npos ? "backoff" : "basic", mode, ok, kSeeds);
    CHECK(unfinished == 0);
    CHECK(duplicates == 0);
    CHECK(ok >= kSeeds * 97 / 100);
}

TEST_CASE("Stress: slow modem settings (SF10, SF12) still exchange acknowledged messages", "[.stress]") {
    struct Setting { int sf; bool ldro; };
    Setting setting = GENERATE(Setting{10, false}, Setting{12, true});
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    sim::LoRaParams params;
    params.sf = setting.sf;
    params.lowDataRateOptimize = setting.ldro;
    int ok = 0, unfinished = 0;
    const int seeds = 40;
    for (int seed = 1; seed <= seeds; ++seed) {
        sim::World world(static_cast<uint64_t>(seed), params);
        Node<LoRaBackoffLink> alice(world, "alice", mode);
        Node<LoRaBackoffLink> bob(world, "bob", mode);
        bool delivered = false;
        // At SF12 a frame takes more than a second, so everything is slower.
        world.spawn([&] {
            alice.rollCall.begin();
            alice.pump(60000, 20000);
            delivered = alice.messenger.sendMessage("bob", "slow but sure", true, 30000);
            alice.pump(200000, 20000);
        });
        world.spawn([&] { bob.rollCall.begin(); bob.pump(200000, 20000); }, 1500);
        if (!world.run(600000)) ++unfinished;
        if (delivered && bob.countContent("slow but sure") == 1) ++ok;
    }
    std::printf("  %-34s SF%-6d %-10s %3d / %3d  (%.1f%%)\n", "acknowledged message, slow modem", setting.sf, modeName(mode), ok, seeds, 100.0 * ok / seeds);
    CHECK(unfinished == 0);
    CHECK(ok >= seeds * 90 / 100);
}
