// Tests for the simulator itself. If these are wrong, nothing built on top
// of the simulator can be trusted.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sim/SimRadio.h"

#include <string>
#include <vector>

using sim::RxMode;

namespace {
std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }
}

TEST_CASE("Sim: time on air matches the Semtech formula", "[sim][airtime]") {
    sim::LoRaParams p;
    p.sf = 7; p.bandwidthHz = 125000; p.codingRate = 1; p.preambleSymbols = 8;
    // Reference values from the Semtech LoRa calculator / TTN airtime calculator.
    REQUIRE(sim::airtimeUs(p, 10) == 41216);
    REQUIRE(sim::airtimeUs(p, 51) == 102656);

    p.sf = 12; p.lowDataRateOptimize = true;
    REQUIRE(sim::airtimeUs(p, 10) == 991232);

    p = sim::LoRaParams();   // library default: SF7, 125 kHz, 12 symbol preamble
    // A 9 byte frame (the link layer ACK) takes about 45 ms.
    REQUIRE(sim::airtimeUs(p, 9) == 45312);
    REQUIRE(sim::symbolUs(p) == 1024);
}

TEST_CASE("Sim: virtual clock and sleep", "[sim]") {
    sim::World world;
    std::vector<uint32_t> stamps;
    world.spawn([&] {
        stamps.push_back(sim::nowMs());
        sim::sleepMs(250);
        stamps.push_back(sim::nowMs());
        sim::sleepMs(0);
        stamps.push_back(sim::nowMs());
    });
    world.spawn([&] {
        sim::sleepMs(100);
        stamps.push_back(sim::nowMs());
    }, 50);
    REQUIRE(world.run(10000));
    REQUIRE(stamps == std::vector<uint32_t>{0, 150, 250, 250});
    REQUIRE(world.errors().empty());
}

TEST_CASE("Sim: a listening node receives, a sender never hears itself", "[sim]") {
    for (RxMode mode : {RxMode::Windowed, RxMode::Continuous}) {
        sim::World world;
        auto& a = world.addRadio(mode);
        auto& b = world.addRadio(mode);
        int aGot = -1, bGot = -1;
        std::vector<uint8_t> bData;
        uint32_t bTime = 0;

        world.spawn([&] {
            auto msg = bytes("hello");
            a.send(msg.data(), msg.size());
            uint8_t buf[64];
            aGot = a.receive(buf, sizeof(buf), 500);   // must not read back its own packet
        }, 10);
        world.spawn([&] {
            uint8_t buf[64];
            bGot = b.receive(buf, sizeof(buf), 1000);
            bTime = sim::nowMs();
            if (bGot > 0) bData.assign(buf, buf + bGot);
        });
        REQUIRE(world.run(10000));
        CHECK(aGot == 0);
        CHECK(bGot == 5);
        CHECK(bData == bytes("hello"));
        // 10 ms start + 1 ms TX setup + 35 ms on air for 5 bytes.
        CHECK(bTime == 46);
        CHECK(world.transmissions().size() == 1);
        CHECK(world.transmissions()[0].deliveredTo == 1);
    }
}

TEST_CASE("Sim: windowed radio is deaf outside receive()", "[sim]") {
    sim::World world;
    auto& a = world.addRadio(RxMode::Windowed);
    auto& b = world.addRadio(RxMode::Windowed);
    int got = -1;

    world.spawn([&] {
        auto msg = bytes("hello");
        a.send(msg.data(), msg.size());
    });
    world.spawn([&] {
        sim::sleepMs(100);                 // asleep while A transmits
        uint8_t buf[64];
        got = b.receive(buf, sizeof(buf), 500);
    });
    REQUIRE(world.run(10000));
    CHECK(got == 0);
    CHECK(world.transmissions()[0].missedBy == 1);
}

TEST_CASE("Sim: windowed radio loses a packet that straddles two receive windows", "[sim]") {
    sim::World world;
    auto& a = world.addRadio(RxMode::Windowed);
    auto& b = world.addRadio(RxMode::Windowed);
    int total = 0;

    world.spawn([&] {
        sim::sleepMs(10);
        auto msg = bytes("hello");         // on air from ~11 ms to ~54 ms
        a.send(msg.data(), msg.size());
    });
    world.spawn([&] {
        uint8_t buf[64];
        for (int i = 0; i < 10; ++i) total += b.receive(buf, sizeof(buf), 20);   // 20 ms windows
    });
    REQUIRE(world.run(10000));
    CHECK(total == 0);
}

TEST_CASE("Sim: continuous radio buffers a packet that arrives between receive() calls", "[sim]") {
    sim::World world;
    auto& a = world.addRadio(RxMode::Continuous);
    auto& b = world.addRadio(RxMode::Continuous);
    int got = -1;

    world.spawn([&] {
        auto msg = bytes("hello");
        a.send(msg.data(), msg.size());
    });
    world.spawn([&] {
        sim::sleepMs(200);
        uint8_t buf[64];
        got = b.receive(buf, sizeof(buf), 10);
    });
    REQUIRE(world.run(10000));
    CHECK(got == 5);
}

TEST_CASE("Sim: half duplex - a transmitting node misses what arrives meanwhile", "[sim]") {
    sim::World world;
    auto& a = world.addRadio(RxMode::Continuous);
    auto& b = world.addRadio(RxMode::Continuous);
    auto& c = world.addRadio(RxMode::Continuous);
    c.begin();
    world.setConnected(0, 2, false);       // C cannot hear A, so only B's view matters
    int got = -1;

    world.spawn([&] {
        auto msg = bytes("from A to B");
        a.send(msg.data(), msg.size());
    });
    world.spawn([&] {
        auto msg = bytes("B is busy talking");
        b.send(msg.data(), msg.size());     // overlaps A's transmission
        uint8_t buf[64];
        got = b.receive(buf, sizeof(buf), 300);
    }, 5);
    REQUIRE(world.run(10000));
    CHECK(got == 0);
}

TEST_CASE("Sim: overlapping transmissions collide", "[sim]") {
    sim::World world;
    auto& a = world.addRadio();
    auto& b = world.addRadio();
    auto& c = world.addRadio();
    int got = -1;

    world.spawn([&] { auto m = bytes("aaaaaaaa"); a.send(m.data(), m.size()); });
    world.spawn([&] { auto m = bytes("bbbbbbbb"); b.send(m.data(), m.size()); }, 20);
    world.spawn([&] {
        uint8_t buf[64];
        got = c.receive(buf, sizeof(buf), 1000);
    });
    REQUIRE(world.run(10000));
    CHECK(got == 0);
    CHECK(world.transmissions()[0].collidedAt >= 1);
    CHECK(world.transmissions()[1].collidedAt >= 1);
}

TEST_CASE("Sim: hidden node topology", "[sim]") {
    sim::World world;
    auto& a = world.addRadio();
    auto& b = world.addRadio();
    auto& c = world.addRadio();
    world.setConnected(0, 2, false);       // A and C cannot hear each other
    int bGot = -1, cGot = -1;

    world.spawn([&] { auto m = bytes("ping"); a.send(m.data(), m.size()); }, 5);
    world.spawn([&] { uint8_t buf[64]; bGot = b.receive(buf, sizeof(buf), 500); });
    world.spawn([&] { uint8_t buf[64]; cGot = c.receive(buf, sizeof(buf), 500); });
    REQUIRE(world.run(10000));
    CHECK(bGot == 4);
    CHECK(cGot == 0);
}

TEST_CASE("Sim: loss probability and delivery filter", "[sim]") {
    sim::World world(42);
    auto& a = world.addRadio();
    auto& b = world.addRadio();
    world.setLossProbability(0.5);
    int received = 0;

    world.spawn([&] {
        for (int i = 0; i < 200; ++i) {
            uint8_t m[4] = {1, 2, 3, static_cast<uint8_t>(i)};
            a.send(m, sizeof(m));
            sim::sleepMs(50);
        }
    }, 5);
    world.spawn([&] {
        uint8_t buf[64];
        while (sim::nowMs() < 25000) {
            if (b.receive(buf, sizeof(buf), 1000) > 0) ++received;
        }
    });
    REQUIRE(world.run(60000));
    CHECK(received > 60);
    CHECK(received < 140);

    // Corruption hook.
    sim::World world2;
    auto& a2 = world2.addRadio();
    auto& b2 = world2.addRadio();
    world2.setDeliveryFilter([](const sim::Transmission&, int, std::vector<uint8_t>& bytes) {
        bytes[0] ^= 0xFF;
        return true;
    });
    uint8_t first = 0;
    world2.spawn([&] { uint8_t m[2] = {0x0F, 0x00}; a2.send(m, 2); }, 5);
    world2.spawn([&] { uint8_t buf[8]; if (b2.receive(buf, 8, 500) > 0) first = buf[0]; });
    REQUIRE(world2.run(10000));
    CHECK(first == 0xF0);
}

TEST_CASE("Sim: runs are deterministic", "[sim]") {
    auto runOnce = [](uint64_t seed) {
        sim::World world(seed);
        auto& a = world.addRadio();
        auto& b = world.addRadio();
        world.setLossProbability(0.3);
        std::vector<int> log;
        world.spawn([&] {
            for (int i = 0; i < 50; ++i) {
                uint8_t m[3] = {7, 7, static_cast<uint8_t>(i)};
                a.send(m, 3);
                sim::sleepMs(10 + sim::rand16() % 50);
            }
        }, 3);
        world.spawn([&] {
            uint8_t buf[16];
            while (sim::nowMs() < 8000) {
                if (b.receive(buf, sizeof(buf), 300) > 0) log.push_back(buf[2]);
            }
        });
        world.run(20000);
        return log;
    };
    CHECK(runOnce(7) == runOnce(7));
    CHECK(runOnce(7) != runOnce(8));
}

TEST_CASE("Sim: time limit unwinds blocked programs and reports it", "[sim]") {
    sim::World world;
    auto& a = world.addRadio();
    bool reachedEnd = false;
    world.spawn([&] {
        uint8_t buf[8];
        for (;;) a.receive(buf, sizeof(buf), 1000);
        reachedEnd = true;
    });
    CHECK_FALSE(world.run(5000));
    CHECK_FALSE(reachedEnd);
}

TEST_CASE("Sim: exceptions in a node program are captured, not fatal", "[sim]") {
    sim::World world;
    world.spawn([] { throw std::runtime_error("boom"); });
    world.run(1000);
    REQUIRE(world.errors().size() == 1);
    CHECK(world.errors()[0].find("boom") != std::string::npos);
}
