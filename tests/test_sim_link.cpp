// Link layer behaviour on a simulated half-duplex LoRa channel.
//
// Every scenario runs for both link implementations and for both radio driver
// models (see tests/sim/SimRadio.h). Node IDs are chosen so that the high and
// low bytes differ, which catches byte order mistakes.
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "LoraBackoffLink.h"
#include "LoraBasicLink.h"
#include "sim/SimHelpers.h"
#include "sim/SimRadio.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

using sim::RxMode;
using namespace simtest;

namespace {
const uint16_t A = 0x0A01;
const uint16_t B = 0x0B02;
const uint16_t C = 0x0C03;

const char* modeName(RxMode m) { return m == RxMode::Windowed ? "windowed radio" : "continuous radio"; }
}

TEMPLATE_TEST_CASE("Link: unicast without ACK is delivered once", "[simlink]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world;
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    bool sent = false;
    std::vector<Rx> got;

    world.spawn([&] { sent = sendText(la, A, B, "hello"); }, 50);
    world.spawn([&] { listenUntil(lb, 1000, got); });
    REQUIRE(world.run(20000));
    REQUIRE(world.errors().empty());
    CHECK(sent);
    REQUIRE(got.size() == 1);
    CHECK(got[0].payload == "hello");
    CHECK(got[0].src == A);
}

TEMPLATE_TEST_CASE("Link: acknowledged send succeeds when the peer is listening", "[simlink][ack]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world;
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    bool acked = false;
    std::vector<Rx> got;

    world.spawn([&] { acked = sendText(la, A, B, "ping", true); }, 50);
    world.spawn([&] { listenUntil(lb, 3000, got); });
    REQUIRE(world.run(20000));
    REQUIRE(world.errors().empty());
    CHECK(acked);
    CHECK(countPayload(got, "ping") == 1);
    // One data frame and one ACK: no retransmission should have been needed.
    CHECK(ra.txCount == 1);
    CHECK(rb.txCount == 1);
}

TEMPLATE_TEST_CASE("Link: acknowledged send retries, then fails, when nobody answers", "[simlink][ack]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world;
    auto& ra = world.addRadio(mode);
    world.addRadio(mode);                 // a silent bystander
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    bool acked = true;

    world.spawn([&] { acked = sendText(la, A, B, "anyone?", true, 3); });
    REQUIRE(world.run(60000));
    CHECK_FALSE(acked);
    CHECK(ra.txCount == 3);
}

TEMPLATE_TEST_CASE("Link: a lost ACK causes a retransmission that is not delivered twice", "[simlink][ack][dup]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world;
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);

    int acksDropped = 0;
    world.setDeliveryFilter([&](const sim::Transmission& tx, int, std::vector<uint8_t>&) {
        FrameView f = parse(tx.bytes);
        if (f.valid && (f.flags & FLAG_ACK) && acksDropped < 1) {
            ++acksDropped;
            return false;                 // the first ACK never arrives
        }
        return true;
    });

    bool acked = false;
    std::vector<Rx> got;
    world.spawn([&] { acked = sendText(la, A, B, "once only", true, 3); }, 50);
    world.spawn([&] { listenUntil(lb, 5000, got); });
    REQUIRE(world.run(30000));
    REQUIRE(world.errors().empty());
    CHECK(acksDropped == 1);
    CHECK(acked);
    CHECK(ra.txCount == 2);               // original + one retransmission
    CHECK(countPayload(got, "once only") == 1);
}

TEMPLATE_TEST_CASE("Link: a lost data frame is retried and delivered once", "[simlink][ack]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world;
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);

    int dataDropped = 0;
    world.setDeliveryFilter([&](const sim::Transmission& tx, int, std::vector<uint8_t>&) {
        FrameView f = parse(tx.bytes);
        if (f.valid && !(f.flags & FLAG_ACK) && dataDropped < 1) {
            ++dataDropped;
            return false;
        }
        return true;
    });

    bool acked = false;
    std::vector<Rx> got;
    world.spawn([&] { acked = sendText(la, A, B, "retry me", true, 3); }, 50);
    world.spawn([&] { listenUntil(lb, 5000, got); });
    REQUIRE(world.run(30000));
    CHECK(acked);
    CHECK(ra.txCount == 2);
    CHECK(countPayload(got, "retry me") == 1);
}

TEMPLATE_TEST_CASE("Link: two nodes sending acknowledged packets to each other both succeed", "[simlink][ack]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world(3);
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    bool ackA = false, ackB = false;
    std::vector<Rx> gotA, gotB;

    // Both nodes send, then listen. The start times differ so the first frames
    // do not collide; the interesting part is what each node does with the
    // other's data frame while it is waiting for its own ACK.
    world.spawn([&] {
        ackA = sendText(la, A, B, "from A", true, 5);
        listenUntil(la, 8000, gotA);
    }, 100);
    world.spawn([&] {
        listenUntil(lb, 130, gotB, 30);
        ackB = sendText(lb, B, A, "from B", true, 5);
        listenUntil(lb, 8000, gotB);
    });
    REQUIRE(world.run(60000));
    REQUIRE(world.errors().empty());
    CHECK(ackA);
    CHECK(ackB);
    CHECK(countPayload(gotA, "from B") == 1);
    CHECK(countPayload(gotB, "from A") == 1);
}

TEMPLATE_TEST_CASE("Link: receivePacket keeps listening for its whole timeout after foreign traffic", "[simlink][timeout]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world;
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    int n = -1;
    uint32_t returnedAt = 0;
    std::string payload;

    world.spawn([&] {
        sendText(la, A, C, "not for B");
        sim::sleepMs(300);
        sendText(la, A, B, "for B");
    }, 20);
    world.spawn([&] {
        uint8_t buf[255];
        uint16_t src;
        n = lb.receivePacket(&src, buf, 255, 1000);   // one call, one second
        returnedAt = sim::nowMs();
        if (n > 0) payload.assign(reinterpret_cast<char*>(buf), n);
    });
    REQUIRE(world.run(20000));
    CHECK(payload == "for B");
    CHECK(returnedAt > 300);
}

TEMPLATE_TEST_CASE("Link: receivePacket returns nothing only after the full timeout", "[simlink][timeout]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    sim::World world;
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    int n = -1;
    uint32_t returnedAt = 0;

    world.spawn([&] {
        for (int i = 0; i < 5; ++i) {
            sendText(la, A, C, "chatter for someone else");
            sim::sleepMs(60);
        }
    }, 10);
    world.spawn([&] {
        uint8_t buf[255];
        uint16_t src;
        n = lb.receivePacket(&src, buf, 255, 800);
        returnedAt = sim::nowMs();
    });
    REQUIRE(world.run(20000));
    CHECK(n == 0);
    CHECK(returnedAt >= 800);
    CHECK(returnedAt <= 810);
}

TEMPLATE_TEST_CASE("Link: only a genuine ACK ends the wait", "[simlink][ack][spoof]", LoRaBasicLink, LoRaBackoffLink) {
    // While A waits for B's ACK, a third node transmits frames that look like
    // ACKs but are not the right one. None of them may be accepted.
    struct Forgery { const char* what; uint16_t dst; uint16_t src; int seqOffset; bool corruptCrc; uint8_t flags; };
    Forgery forgery = GENERATE(
        Forgery{"ACK from a different node", A, C, 0, false, FLAG_ACK},
        Forgery{"ACK addressed to a different node", C, B, 0, false, FLAG_ACK},
        Forgery{"ACK with the wrong sequence number", A, B, 1, false, FLAG_ACK},
        Forgery{"ACK with a bad CRC", A, B, 0, true, FLAG_ACK},
        Forgery{"data frame with the same sequence number", A, B, 0, false, 0});
    INFO(forgery.what);
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    sim::World world;
    auto& ra = world.addRadio(mode);
    auto& rc = world.addRadio(mode);      // the forger; B does not exist
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    bool acked = true;
    uint8_t seqSeen = 0;
    bool sawData = false;

    world.setTxObserver([&](const sim::Transmission& tx) {
        FrameView f = parse(tx.bytes);
        if (tx.src == 0 && f.valid && !sawData) { seqSeen = f.seq; sawData = true; }
    });
    world.spawn([&] { acked = sendText(la, A, B, "data", true, 1); }, 10);
    world.spawn([&] {
        // Listen for A's frame, then answer with the forgery immediately.
        uint8_t buf[255];
        bool heard = false;
        while (!heard && sim::nowMs() < 400) heard = rc.receive(buf, sizeof(buf), 400) > 0;
        if (!heard) return;
        auto f = frame(forgery.dst, forgery.src, static_cast<uint8_t>(seqSeen + forgery.seqOffset), forgery.flags);
        if (forgery.corruptCrc) f.back() ^= 0x5A;
        rc.send(f.data(), f.size());
    });
    REQUIRE(world.run(20000));
    REQUIRE(sawData);
    REQUIRE(rc.txCount == 1);
    CHECK_FALSE(acked);
}

TEMPLATE_TEST_CASE("Link: broadcast never waits for an ACK", "[simlink][broadcast]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    sim::World world;
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    auto& rc = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    TestType lc(&rc, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    lc.setLocalId(C);
    bool ok = false;
    uint32_t took = 0;
    std::vector<Rx> gotB, gotC;

    world.spawn([&] {
        uint32_t t0 = sim::nowMs();
        ok = sendText(la, A, 0xFFFF, "to everyone", true, 3);
        took = sim::nowMs() - t0;
    }, 50);
    world.spawn([&] { listenUntil(lb, 2000, gotB); });
    world.spawn([&] { listenUntil(lc, 2000, gotC); });
    REQUIRE(world.run(20000));
    CHECK(ok);
    CHECK(took < 150);
    CHECK(ra.txCount == 1);
    CHECK(rb.txCount == 0);               // nobody ACKs a broadcast
    CHECK(rc.txCount == 0);
    CHECK(countPayload(gotB, "to everyone") == 1);
    CHECK(countPayload(gotC, "to everyone") == 1);
}

TEMPLATE_TEST_CASE("Link: maxRetries below one still transmits once", "[simlink]", LoRaBasicLink, LoRaBackoffLink) {
    int maxRetries = GENERATE(0, -1);
    sim::World world;
    auto& ra = world.addRadio();
    auto& rb = world.addRadio();
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    bool ok = false;
    std::vector<Rx> got;

    world.spawn([&] { ok = sendText(la, A, B, "one shot", false, maxRetries); }, 50);
    world.spawn([&] { listenUntil(lb, 1000, got); });
    REQUIRE(world.run(20000));
    CHECK(ok);
    CHECK(ra.txCount == 1);
    CHECK(countPayload(got, "one shot") == 1);
}

TEMPLATE_TEST_CASE("Link: a payload larger than the caller's buffer is dropped, not truncated or over-reported", "[simlink][bounds]", LoRaBasicLink, LoRaBackoffLink) {
    sim::World world;
    auto& ra = world.addRadio();
    auto& rb = world.addRadio();
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    int n = -1;
    uint8_t buf[8];
    std::fill(buf, buf + 8, 0xEE);

    world.spawn([&] { sendText(la, A, B, "0123456789"); }, 50);
    world.spawn([&] {
        uint16_t src;
        n = lb.receivePacket(&src, buf, 4, 1000);
    });
    REQUIRE(world.run(20000));
    CHECK(n == 0);
    CHECK(buf[4] == 0xEE);                // nothing written past maxLen
}

TEST_CASE("Link: Basic and Backoff links use the same frame format", "[simlink][interop]") {
    bool basicSends = GENERATE(true, false);
    sim::World world;
    auto& ra = world.addRadio();
    auto& rb = world.addRadio();
    LoRaBasicLink basic(basicSends ? &ra : &rb, sim::nowMs, sim::sleepMs);
    LoRaBackoffLink backoff(basicSends ? &rb : &ra, sim::nowMs, sim::sleepMs);
    ILoRaLink& tx = basicSends ? static_cast<ILoRaLink&>(basic) : backoff;
    ILoRaLink& rx = basicSends ? static_cast<ILoRaLink&>(backoff) : basic;
    tx.setLocalId(A);
    rx.setLocalId(B);
    bool acked = false;
    std::vector<Rx> got;

    world.spawn([&] { acked = sendText(tx, A, B, "interop", true, 3); }, 50);
    world.spawn([&] { listenUntil(rx, 3000, got); });
    REQUIRE(world.run(20000));
    CHECK(acked);
    REQUIRE(countPayload(got, "interop") == 1);
    CHECK(got[0].src == A);
}

TEMPLATE_TEST_CASE("Link: frames on the air use the documented big-endian layout", "[simlink][interop]", LoRaBasicLink, LoRaBackoffLink) {
    sim::World world;
    auto& ra = world.addRadio();
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    world.spawn([&] { sendText(la, A, B, "xyz"); });
    REQUIRE(world.run(5000));
    REQUIRE(world.transmissions().size() == 1);
    const auto& bytes = world.transmissions()[0].bytes;
    auto expected = frame(B, A, bytes.size() > 4 ? bytes[4] : 0, 0, "xyz");
    CHECK(bytes == expected);
}

TEMPLATE_TEST_CASE("Link: ACK matching uses the source ID of the packet", "[simlink][ack]", LoRaBasicLink, LoRaBackoffLink) {
    // The sender passes its ID to sendPacket() but never calls setLocalId().
    sim::World world;
    auto& ra = world.addRadio();
    auto& rb = world.addRadio();
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    lb.setLocalId(B);
    bool acked = false;
    std::vector<Rx> got;

    world.spawn([&] { acked = sendText(la, A, B, "who am I", true, 3); }, 50);
    world.spawn([&] { listenUntil(lb, 3000, got); });
    REQUIRE(world.run(20000));
    CHECK(acked);
    CHECK(ra.txCount == 1);
}

TEMPLATE_TEST_CASE("Link: three nodes exchanging acknowledged packets lose nothing", "[simlink][soak]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    const int kMessages = 10;
    sim::World world(11);
    sim::SimRadio* radios[3] = {&world.addRadio(mode), &world.addRadio(mode), &world.addRadio(mode)};
    const uint16_t ids[3] = {A, B, C};
    TestType l0(radios[0], sim::nowMs, sim::sleepMs);
    TestType l1(radios[1], sim::nowMs, sim::sleepMs);
    TestType l2(radios[2], sim::nowMs, sim::sleepMs);
    TestType* links[3] = {&l0, &l1, &l2};
    std::vector<Rx> got[3];
    int acked[3] = {0, 0, 0};

    for (int i = 0; i < 3; ++i) {
        links[i]->setLocalId(ids[i]);
        world.spawn([&, i] {
            int next = (i + 1) % 3;
            for (int m = 0; m < kMessages; ++m) {
                // Listen for a pseudo-random while, then send one message.
                listenUntil(*links[i], sim::nowMs() + 200 + sim::rand16() % 400, got[i], 100);
                std::string text = "n" + std::to_string(i) + "m" + std::to_string(m);
                if (sendText(*links[i], ids[i], ids[next], text, true, 8)) ++acked[i];
            }
            listenUntil(*links[i], 60000, got[i], 250);
        }, static_cast<uint32_t>(i * 37));
    }
    REQUIRE(world.run(120000));
    REQUIRE(world.errors().empty());
    for (int i = 0; i < 3; ++i) {
        int prev = (i + 2) % 3;
        INFO("node " << i);
        CHECK(acked[i] == kMessages);
        std::multiset<std::string> seen;
        for (auto& r : got[i]) seen.insert(r.payload);
        for (int m = 0; m < kMessages; ++m) {
            std::string text = "n" + std::to_string(prev) + "m" + std::to_string(m);
            INFO(text);
            CHECK(seen.count(text) == 1);
        }
        CHECK(got[i].size() == static_cast<size_t>(kMessages));
    }
}

TEMPLATE_TEST_CASE("Link: four nodes that transmit at the same instant all get through", "[simlink][burst]", LoRaBasicLink, LoRaBackoffLink) {
    // The worst case for a shared channel: every first transmission collides.
    // Exponential backoff has to pull the retries apart.
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    uint64_t seed = GENERATE(1, 2, 3, 4, 5);
    INFO(modeName(mode) << ", seed " << seed);
    const int kSenders = 4;
    const uint16_t sink = 0x5151;
    sim::World world(seed);
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
        }, 100);                           // all at exactly the same time
    }
    world.spawn([&] {
        while (finished < kSenders) listenUntil(lsink, sim::nowMs() + 2000, got, 2000);
    });
    REQUIRE(world.run(300000));
    REQUIRE(world.errors().empty());
    for (int i = 0; i < kSenders; ++i) {
        INFO("sender " << i);
        CHECK(acked[i]);
        CHECK(countPayload(got, "burst" + std::to_string(i)) == 1);
    }
    CHECK(got.size() == static_cast<size_t>(kSenders));
}

TEMPLATE_TEST_CASE("Link: duplicate suppression never drops distinct packets, even when the sequence number wraps", "[simlink][dup]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world;
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    const int kCount = 300;               // more than 256, so seq wraps
    int acked = 0;
    std::vector<Rx> got;
    bool done = false;

    world.spawn([&] {
        for (int i = 0; i < kCount; ++i) {
            // Identical payloads on purpose: only the sequence number differs.
            if (sendText(la, A, B, "same", true, 8)) ++acked;
        }
        done = true;
    }, 50);
    world.spawn([&] {
        while (!done) listenUntil(lb, sim::nowMs() + 500, got, 500);
    });
    REQUIRE(world.run(1200000));
    CHECK(acked == kCount);
    // Every acknowledged frame was delivered exactly once.
    CHECK(static_cast<int>(got.size()) == kCount);
    CHECK(lb.stats().rxFrames == static_cast<uint32_t>(kCount));
}

TEMPLATE_TEST_CASE("Link: garbage on the air is ignored", "[simlink][fuzz]", LoRaBasicLink, LoRaBackoffLink) {
    sim::World world(99);
    auto& ra = world.addRadio();
    auto& rb = world.addRadio();
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    lb.setLocalId(B);
    std::vector<Rx> got;
    bool done = false;
    const int kFrames = 2000;

    world.spawn([&] {
        std::mt19937 rng(1234);
        for (int i = 0; i < kFrames; ++i) {
            uint8_t junk[255];
            size_t len = 1 + rng() % 255;
            for (size_t k = 0; k < len; ++k) junk[k] = static_cast<uint8_t>(rng());
            if (i % 3 == 0) {            // make some of it look addressed to B, with a plausible length byte
                junk[0] = B >> 8; junk[1] = B & 0xFF;
                if (len >= 9) junk[6] = static_cast<uint8_t>(len - 9);
            }
            ra.send(junk, len);
        }
        done = true;
    }, 10);
    world.spawn([&] {
        while (!done) listenUntil(lb, sim::nowMs() + 1000, got, 1000);
    });
    REQUIRE(world.run(3600000));
    REQUIRE(world.errors().empty());
    // With a 16 bit CRC a random frame passes about once in 65536 tries.
    CHECK(got.size() <= 1);
    CHECK(rb.txCount <= 1);               // and B does not ACK garbage
}

TEMPLATE_TEST_CASE("Link: zero timeout polls without blocking", "[simlink][timeout]", LoRaBasicLink, LoRaBackoffLink) {
    sim::World world;
    auto& rb = world.addRadio();
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    lb.setLocalId(B);
    int n = -1;
    uint32_t t = 99;
    world.spawn([&] {
        uint8_t buf[255];
        uint16_t src;
        n = lb.receivePacket(&src, buf, 255, 0);
        t = sim::nowMs();
    });
    REQUIRE(world.run(1000));
    CHECK(n == 0);
    CHECK(t == 0);
}

TEMPLATE_TEST_CASE("Link: slow modem settings need a longer ACK timeout, taken from the radio when it can report it", "[simlink][ack][slow]", LoRaBasicLink, LoRaBackoffLink) {
    // At SF11 / 125 kHz the 9 byte ACK alone is on the air for about 560 ms,
    // longer than the default ACK timeout.
    sim::LoRaParams params;
    params.sf = 11;
    params.lowDataRateOptimize = true;
    REQUIRE(sim::airtimeUs(params, 9) > 500000);

    enum Case { RadioReportsAirtime, NoReportDefaultTimeout, NoReportConfiguredTimeout };
    Case which = GENERATE(RadioReportsAirtime, NoReportDefaultTimeout, NoReportConfiguredTimeout);
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO("case " << which << ", " << modeName(mode));

    sim::World world(1, params);
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    if (which != RadioReportsAirtime) {
        ra.setReportsAirtime(false);
        rb.setReportsAirtime(false);
    }
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    if (which == NoReportConfiguredTimeout) {
        la.setAckTimeoutMs(2000);
        CHECK(la.ackTimeoutMs() == 2000);
    }
    bool acked = false;
    std::vector<Rx> got;
    bool done = false;

    world.spawn([&] {
        acked = sendText(la, A, B, "slow", true, 1);
        done = true;
    }, 100);
    world.spawn([&] { while (!done) listenUntil(lb, sim::nowMs() + 5000, got, 5000); });
    REQUIRE(world.run(120000));

    CHECK(countPayload(got, "slow") == 1);          // the data always arrives
    if (which == NoReportDefaultTimeout) {
        CHECK_FALSE(acked);                         // ...but the sender gives up before the ACK lands
    } else {
        CHECK(acked);
    }
}

TEMPLATE_TEST_CASE("Link: statistics count what happened", "[simlink][stats]", LoRaBasicLink, LoRaBackoffLink) {
    sim::World world(2);
    auto& ra = world.addRadio(RxMode::Continuous);
    auto& rb = world.addRadio(RxMode::Continuous);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);

    int acksDropped = 0;
    world.setDeliveryFilter([&](const sim::Transmission& tx, int, std::vector<uint8_t>&) {
        FrameView f = parse(tx.bytes);
        if (f.valid && (f.flags & FLAG_ACK) && acksDropped < 1) { ++acksDropped; return false; }
        return true;
    });
    std::vector<Rx> got;
    world.spawn([&] {
        sendText(la, A, B, "one", true, 3);          // first ACK lost -> one retransmission
        sendText(la, A, C, "nobody home", true, 2);  // fails after two transmissions
        sendText(la, A, 0xFFFF, "all");
        uint8_t junk[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        ra.send(junk, sizeof(junk));                 // not a valid frame
    }, 50);
    world.spawn([&] { listenUntil(lb, 20000, got, 5000); });
    REQUIRE(world.run(60000));

    const auto& sa = la.stats();
    const auto& sb = lb.stats();
    CHECK(sa.txFrames == 5);          // one + retransmission, two for C, one broadcast
    CHECK(sa.txRetransmits == 2);
    CHECK(sa.rxAcks == 1);
    CHECK(sa.txFailed == 1);
    CHECK(sb.rxFrames == 2);          // "one" and "all"
    CHECK(sb.rxDuplicates == 1);
    CHECK(sb.txAcks == 2);            // the original and the duplicate are both acknowledged
    CHECK(sb.rxForeign == 2);         // both frames for C
    CHECK(sb.rxInvalid == 1);
    CHECK(got.size() == 2);
    CHECK(lb.pendingCount() == 0);
}

TEMPLATE_TEST_CASE("Link: frames that arrive while a node is busy sending are queued, and never acknowledged unless kept", "[simlink][queue]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    INFO(modeName(mode));
    sim::World world(4);
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs);
    TestType lb(&rb, sim::nowMs, sim::sleepMs);
    la.setLocalId(A);
    lb.setLocalId(B);
    const int kCount = 10;                 // more than the queue holds
    std::vector<std::string> ackedByB;
    std::vector<Rx> gotA;
    bool aBusyResult = true;

    world.spawn([&] {
        // A spends a long time trying to reach a node that does not exist...
        aBusyResult = sendText(la, A, C, "are you there?", true, 12);
        // ...and only afterwards reads what arrived in the meantime.
        listenUntil(la, sim::nowMs() + 15000, gotA, 3000);
    }, 10);
    world.spawn([&] {
        sim::sleepMs(700);
        for (int i = 0; i < kCount; ++i) {
            std::string text = "queued" + std::to_string(i);
            if (sendText(lb, B, A, text, true, 4)) ackedByB.push_back(text);
        }
    });
    REQUIRE(world.run(300000));
    REQUIRE(world.errors().empty());
    CHECK_FALSE(aBusyResult);

    // The invariant: whatever B was told had been delivered really was
    // delivered, exactly once, and in order. Frames that did not fit in A's
    // queue were not acknowledged, so B retried them.
    std::vector<std::string> received;
    for (auto& r : gotA) received.push_back(r.payload);
    for (auto& text : ackedByB) {
        INFO(text);
        CHECK(std::count(received.begin(), received.end(), text) == 1);
    }
    std::set<std::string> unique(received.begin(), received.end());
    CHECK(unique.size() == received.size());                    // no duplicates
    CHECK(std::is_sorted(received.begin(), received.end()));    // in order (single digit suffixes)
    CHECK(ackedByB.size() >= LORA_LINK_RX_QUEUE_SIZE);          // at least a queue's worth got through while A was busy
    CHECK(la.stats().rxDropped > 0);                            // and the queue did overflow at some point
}
