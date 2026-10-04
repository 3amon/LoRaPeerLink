// EncryptedLoRaLink: wire format, authentication, and end-to-end behaviour
// on the simulated radio.
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "EncryptedLoRaLink.h"
#include "LoraBackoffLink.h"
#include "LoraBasicLink.h"
#include "LplCrypto.h"
#include "sim/SimHelpers.h"
#include "sim/SimNode.h"
#include "sim/SimRadio.h"

#include <cstring>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

using sim::RxMode;
using namespace simtest;

namespace {

const uint16_t A = 0x0A01;
const uint16_t B = 0x0B02;
const uint16_t C = 0x0C03;

std::vector<uint8_t> unhex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

std::string hex(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < len; ++i) {
        s.push_back(digits[data[i] >> 4]);
        s.push_back(digits[data[i] & 15]);
    }
    return s;
}

// A link that goes nowhere, for tests that only use seal() and open().
class NullLink : public ILoRaLink {
public:
    bool sendPacket(uint16_t, uint16_t, const uint8_t*, uint8_t, bool, int) override { return true; }
    int receivePacket(uint16_t*, uint8_t*, uint8_t, uint32_t) override { return 0; }
    void setLocalId(uint16_t) override {}
};

void stuckRandom(uint8_t* buffer, size_t len) { memset(buffer, 0x42, len); }   // a broken random source

} // namespace

TEST_CASE("Encrypted: keys and wire format match an independent implementation", "[encrypted][vectors]") {
    // Reference values produced with Python (hashlib + cryptography):
    //   keys = PBKDF2-HMAC-SHA256("hunter2", salt "LoRaNet", 4096 iterations, 32 bytes)
    //   wire = IV || AES-128-CBC(PKCS7("Hello, LoRa!")) || HMAC-SHA256(macKey, 0x0A01 || IV || C)[:8]
    NullLink null;
    EncryptedLoRaLink link(&null, "LoRaNet", "hunter2", 4096);
    uint8_t keys[32];
    link.exportKeysForTesting(keys);
    CHECK(hex(keys, 32) == "f53a28c90a474b85ed42b2622ec439cad1c9add035705b4db6a27c2ca72856fc");

    uint8_t iv[16];
    for (int i = 0; i < 16; ++i) iv[i] = static_cast<uint8_t>(0xA0 + i);
    const std::string text = "Hello, LoRa!";
    uint8_t wire[64];
    size_t n = link.seal(A, iv, reinterpret_cast<const uint8_t*>(text.data()), text.size(), wire, sizeof(wire));
    REQUIRE(n == 40);
    const std::string expected = "a0a1a2a3a4a5a6a7a8a9aaabacadaeafdbf8256f9df4970161e2dbfbfef9e7524af4dabdd10801e4";
    CHECK(hex(wire, n) == expected);

    // And the reference packet opens.
    auto ref = unhex(expected);
    uint8_t plain[64];
    size_t plainLen = 0;
    REQUIRE(link.open(A, ref.data(), ref.size(), plain, sizeof(plain), plainLen));
    CHECK(std::string(reinterpret_cast<char*>(plain), plainLen) == text);
}

TEST_CASE("Encrypted: every payload length round trips and has the documented size", "[encrypted]") {
    NullLink null;
    EncryptedLoRaLink link(&null, "net", "pw", 8);
    REQUIRE(link.maxPayloadSize() == 207);
    CHECK(link.getMaxPayloadSize() == 207);

    uint8_t plain[255], wire[255], back[255], iv[16];
    for (int i = 0; i < 255; ++i) plain[i] = static_cast<uint8_t>(i * 13 + 5);
    for (int i = 0; i < 16; ++i) iv[i] = static_cast<uint8_t>(i);

    for (size_t len = 0; len <= 207; ++len) {
        INFO("length " << len);
        size_t n = link.seal(A, iv, plain, len, wire, 246);
        REQUIRE(n == 16 + (len / 16 + 1) * 16 + 8);
        REQUIRE(n <= 246);
        size_t got = 999;
        REQUIRE(link.open(A, wire, n, back, sizeof(back), got));
        REQUIRE(got == len);
        REQUIRE(memcmp(back, plain, len) == 0);
    }
    // One byte more does not fit in a link payload.
    CHECK(link.seal(A, iv, plain, 208, wire, 246) == 0);
}

TEST_CASE("Encrypted: any single bit flip is detected", "[encrypted][tamper]") {
    NullLink null;
    EncryptedLoRaLink link(&null, "net", "pw", 8);
    uint8_t iv[16] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6};
    for (size_t len : {1u, 15u, 16u, 40u}) {
        std::vector<uint8_t> plain(len, 0x33);
        uint8_t wire[128];
        size_t n = link.seal(A, iv, plain.data(), len, wire, sizeof(wire));
        REQUIRE(n > 0);
        int accepted = 0;
        for (size_t byte = 0; byte < n; ++byte) {
            for (int bit = 0; bit < 8; ++bit) {
                wire[byte] ^= static_cast<uint8_t>(1 << bit);
                uint8_t out[128];
                size_t outLen = 0;
                if (link.open(A, wire, n, out, sizeof(out), outLen)) ++accepted;
                wire[byte] ^= static_cast<uint8_t>(1 << bit);
            }
        }
        INFO("payload length " << len);
        CHECK(accepted == 0);
    }
}

TEST_CASE("Encrypted: wrong credentials, wrong sender and wrong lengths are rejected", "[encrypted][tamper]") {
    NullLink null;
    EncryptedLoRaLink good(&null, "net", "pw", 8);
    uint8_t iv[16] = {0};
    const std::string text = "attack at dawn";
    uint8_t wire[128], out[128];
    size_t n = good.seal(A, iv, reinterpret_cast<const uint8_t*>(text.data()), text.size(), wire, sizeof(wire));
    size_t outLen = 0;
    REQUIRE(good.open(A, wire, n, out, sizeof(out), outLen));

    EncryptedLoRaLink wrongPassword(&null, "net", "pW", 8);
    EncryptedLoRaLink wrongNetwork(&null, "Net", "pw", 8);
    EncryptedLoRaLink wrongIterations(&null, "net", "pw", 9);
    EncryptedLoRaLink swapped(&null, "pw", "net", 8);
    CHECK_FALSE(wrongPassword.open(A, wire, n, out, sizeof(out), outLen));
    CHECK_FALSE(wrongNetwork.open(A, wire, n, out, sizeof(out), outLen));
    CHECK_FALSE(wrongIterations.open(A, wire, n, out, sizeof(out), outLen));
    CHECK_FALSE(swapped.open(A, wire, n, out, sizeof(out), outLen));

    // The tag covers the source ID: a packet cannot be re-attributed.
    CHECK_FALSE(good.open(B, wire, n, out, sizeof(out), outLen));

    // Truncated, extended, or too short to be a packet at all.
    for (size_t len = 0; len < n; ++len) CHECK_FALSE(good.open(A, wire, len, out, sizeof(out), outLen));
    uint8_t longer[160];
    memcpy(longer, wire, n);
    memset(longer + n, 0, 32);
    CHECK_FALSE(good.open(A, longer, n + 16, out, sizeof(out), outLen));
    CHECK_FALSE(good.open(A, longer, n + 1, out, sizeof(out), outLen));

    // Output buffer too small: refused, nothing written past the buffer.
    uint8_t small[4] = {0xEE, 0xEE, 0xEE, 0xEE};
    CHECK_FALSE(good.open(A, wire, n, small, 3, outLen));
    CHECK(small[3] == 0xEE);

    // Zero iterations behaves as one (and not as "no key").
    EncryptedLoRaLink zero(&null, "net", "pw", 0);
    EncryptedLoRaLink one(&null, "net", "pw", 1);
    uint8_t k0[32], k1[32];
    zero.exportKeysForTesting(k0);
    one.exportKeysForTesting(k1);
    CHECK(memcmp(k0, k1, 32) == 0);
}

TEST_CASE("Encrypted: a packet with a valid tag but invalid padding is rejected", "[encrypted][tamper]") {
    NullLink null;
    EncryptedLoRaLink link(&null, "net", "pw", 8);
    uint8_t keys[32];
    link.exportKeysForTesting(keys);

    // Build a packet by hand: one block whose plaintext ends in 0x00, which
    // is never valid PKCS#7, with a correct tag over it.
    uint8_t wire[40];
    uint8_t iv[16] = {1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 121, 98, 219};
    memcpy(wire, iv, 16);
    uint8_t block[16] = {0};
    for (int i = 0; i < 16; ++i) block[i] = static_cast<uint8_t>(block[i] ^ iv[i]);
    lpl::Aes128 aes(keys);
    aes.encryptBlock(block, wire + 16);
    const uint8_t src[2] = {static_cast<uint8_t>(A >> 8), static_cast<uint8_t>(A & 0xFF)};
    lpl::HmacSha256 h(keys + 16, 16);
    h.update(src, 2);
    h.update(wire, 32);
    uint8_t mac[32];
    h.finish(mac);
    memcpy(wire + 32, mac, 8);

    uint8_t out[64];
    size_t outLen = 0;
    CHECK_FALSE(link.open(A, wire, 40, out, sizeof(out), outLen));
}

TEST_CASE("Encrypted: random bytes are never accepted", "[encrypted][fuzz]") {
    NullLink null;
    EncryptedLoRaLink link(&null, "net", "pw", 8);
    std::mt19937 rng(555);
    int accepted = 0;
    for (int i = 0; i < 200000; ++i) {
        uint8_t wire[255], out[255];
        size_t len = rng() % 247;
        if (i % 2 == 0) len = 24 + 16 * (1 + rng() % 13);     // lengths that are structurally valid
        for (size_t k = 0; k < len; ++k) wire[k] = static_cast<uint8_t>(rng());
        size_t outLen = 0;
        if (link.open(static_cast<uint16_t>(rng()), wire, len, out, sizeof(out), outLen)) ++accepted;
    }
    CHECK(accepted == 0);
}

TEST_CASE("Encrypted: IVs never repeat, even with a broken random source", "[encrypted][iv]") {
    sim::World world;
    auto& ra = world.addRadio();
    LoRaBasicLink raw(&ra, sim::nowMs, sim::sleepMs);
    EncryptedLoRaLink link(&raw, "net", "pw", 8, sim::nowMs, stuckRandom);
    const int kPackets = 300;
    world.spawn([&] {
        for (int i = 0; i < kPackets; ++i) sendText(link, A, 0xFFFF, "the same plaintext every time");
    });
    REQUIRE(world.run(600000));
    REQUIRE(world.transmissions().size() == static_cast<size_t>(kPackets));

    std::set<std::string> ivs, firstBlocks;
    for (auto& tx : world.transmissions()) {
        REQUIRE(tx.bytes.size() == 9 + 16 + 32 + 8);
        ivs.insert(std::string(tx.bytes.begin() + 7, tx.bytes.begin() + 23));
        firstBlocks.insert(std::string(tx.bytes.begin() + 23, tx.bytes.begin() + 39));
    }
    CHECK(ivs.size() == static_cast<size_t>(kPackets));
    CHECK(firstBlocks.size() == static_cast<size_t>(kPackets));   // identical plaintext, different ciphertext
}

TEST_CASE("Encrypted: ciphertext does not reveal the plaintext", "[encrypted]") {
    // Version 1 XORed the payload with the IV (sent in clear) and a key that
    // was mostly zero, so ciphertext XOR IV gave back most of the message.
    NullLink null;
    EncryptedLoRaLink link(&null, "net", "pw", 8);
    const std::string text(64, 'A');
    uint8_t iv[16];
    for (int i = 0; i < 16; ++i) iv[i] = static_cast<uint8_t>(0x10 + i);
    uint8_t wire[128];
    size_t n = link.seal(A, iv, reinterpret_cast<const uint8_t*>(text.data()), text.size(), wire, sizeof(wire));
    REQUIRE(n == 16 + 80 + 8);

    int leaked = 0, repeatedBlocks = 0;
    for (size_t i = 0; i < 64; ++i) {
        if (static_cast<uint8_t>(wire[16 + i] ^ iv[i % 16]) == 'A') ++leaked;
        if (wire[16 + i] == 'A') ++leaked;
    }
    for (int b = 1; b < 4; ++b) {
        if (memcmp(wire + 16, wire + 16 + 16 * b, 16) == 0) ++repeatedBlocks;
    }
    CHECK(leaked <= 3);            // chance level is about 0.5 of 128 comparisons
    CHECK(repeatedBlocks == 0);    // identical plaintext blocks encrypt differently
    CHECK(std::string(reinterpret_cast<char*>(wire), n).find("AAAA") == std::string::npos);
}

TEST_CASE("Encrypted: a replayed packet is still accepted (documented limitation)", "[encrypted][limitation]") {
    NullLink null;
    EncryptedLoRaLink link(&null, "net", "pw", 8);
    uint8_t iv[16] = {0};
    uint8_t wire[64], out[64];
    size_t n = link.seal(A, iv, reinterpret_cast<const uint8_t*>("open the door"), 13, wire, sizeof(wire));
    size_t outLen = 0;
    CHECK(link.open(A, wire, n, out, sizeof(out), outLen));
    CHECK(link.open(A, wire, n, out, sizeof(out), outLen));   // no replay protection in this layer
}

TEMPLATE_TEST_CASE("Encrypted: acknowledged unicast and broadcast work end to end; outsiders get nothing", "[encrypted][sim]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    sim::World world(17);
    auto& ra = world.addRadio(mode);
    auto& rb = world.addRadio(mode);
    auto& rc = world.addRadio(mode);
    TestType la(&ra, sim::nowMs, sim::sleepMs), lb(&rb, sim::nowMs, sim::sleepMs), lc(&rc, sim::nowMs, sim::sleepMs);
    EncryptedLoRaLink ea(&la, "net", "secret", 32, sim::nowMs);
    EncryptedLoRaLink eb(&lb, "net", "secret", 32, sim::nowMs);
    EncryptedLoRaLink ec(&lc, "net", "wrong", 32, sim::nowMs);       // same network name, wrong password
    ea.setLocalId(A);
    eb.setLocalId(B);
    ec.setLocalId(C);
    bool acked = false, broadcastOk = false;
    std::vector<Rx> gotB, gotC;

    world.spawn([&] {
        acked = sendText(ea, A, B, "for B only", true, 3);
        broadcastOk = sendText(ea, A, 0xFFFF, "for the whole network");
    }, 50);
    world.spawn([&] { listenUntil(eb, 4000, gotB); });
    world.spawn([&] { listenUntil(ec, 4000, gotC); });
    REQUIRE(world.run(30000));
    REQUIRE(world.errors().empty());
    CHECK(acked);
    CHECK(broadcastOk);
    REQUIRE(gotB.size() == 2);
    CHECK(gotB[0].payload == "for B only");
    CHECK(gotB[0].src == A);
    CHECK(gotB[1].payload == "for the whole network");
    CHECK(gotC.empty());
    CHECK(ec.rejectedPackets() == 1);       // it heard the broadcast and could not authenticate it

    // On the air: addresses readable, payload not.
    const auto& first = world.transmissions()[0].bytes;
    FrameView f = parse(first);
    REQUIRE(f.valid);
    CHECK(f.dst == B);
    CHECK(f.src == A);
    const std::string onAir(first.begin(), first.end());
    CHECK(onAir.find("for B") == std::string::npos);
}

TEMPLATE_TEST_CASE("Encrypted: payload limits over the air", "[encrypted][sim][bounds]", LoRaBasicLink, LoRaBackoffLink) {
    sim::World world(18);
    auto& ra = world.addRadio(RxMode::Continuous);
    auto& rb = world.addRadio(RxMode::Continuous);
    TestType la(&ra, sim::nowMs, sim::sleepMs), lb(&rb, sim::nowMs, sim::sleepMs);
    EncryptedLoRaLink ea(&la, "net", "secret", 8, sim::nowMs);
    EncryptedLoRaLink eb(&lb, "net", "secret", 8, sim::nowMs);
    ea.setLocalId(A);
    eb.setLocalId(B);
    std::string biggest(207, 'm');
    biggest.front() = '<';
    biggest.back() = '>';
    bool maxOk = false, overOk = true, emptyOk = true, nullOk = true, oneOk = false;
    std::vector<Rx> got;

    world.spawn([&] {
        maxOk = sendText(ea, A, B, biggest, true, 3);
        uint8_t over[208] = {0};
        overOk = ea.sendPacket(A, B, over, 208, false);
        emptyOk = ea.sendPacket(A, B, over, 0, false);
        nullOk = ea.sendPacket(A, B, nullptr, 5, false);
        oneOk = sendText(ea, A, B, "x", true, 3);
    }, 50);
    world.spawn([&] { listenUntil(eb, 6000, got); });
    REQUIRE(world.run(30000));
    CHECK(maxOk);
    CHECK_FALSE(overOk);
    CHECK_FALSE(emptyOk);
    CHECK_FALSE(nullOk);
    CHECK(oneOk);
    REQUIRE(got.size() == 2);
    CHECK(got[0].payload == biggest);
    CHECK(got[1].payload == "x");
    CHECK(ra.txCount == 2);                 // the rejected sends never reached the radio
    // IV (16) + 13 cipher blocks (208) + tag (8) + link header and CRC (9).
    CHECK(world.transmissions()[0].bytes.size() == 241);
}

TEST_CASE("Encrypted: receivePacket keeps listening after rejecting a packet", "[encrypted][sim][timeout]") {
    sim::World world(19);
    auto& ra = world.addRadio();
    auto& rb = world.addRadio();
    auto& rx = world.addRadio();
    LoRaBasicLink la(&ra, sim::nowMs, sim::sleepMs), lb(&rb, sim::nowMs, sim::sleepMs), lx(&rx, sim::nowMs, sim::sleepMs);
    EncryptedLoRaLink ea(&la, "net", "secret", 8, sim::nowMs);
    EncryptedLoRaLink eb(&lb, "net", "secret", 8, sim::nowMs);
    ea.setLocalId(A);
    eb.setLocalId(B);
    int n = -1;
    std::string payload;
    uint16_t from = 0;

    world.spawn([&] {
        // An unencrypted node talks to B first, then the real message follows.
        sendText(lx, C, B, "plain text from a stranger, long enough to look like a packet");
        sim::sleepMs(200);
        sendText(ea, A, B, "the real one");
    }, 20);
    world.spawn([&] {
        uint8_t buf[255];
        n = eb.receivePacket(&from, buf, 255, 2000);        // a single call
        if (n > 0) payload.assign(reinterpret_cast<char*>(buf), n);
    });
    REQUIRE(world.run(30000));
    CHECK(payload == "the real one");
    CHECK(from == A);
    CHECK(eb.rejectedPackets() == 1);
}

TEMPLATE_TEST_CASE("Encrypted: the full stack works over an encrypted link and isolates a node with the wrong password", "[encrypted][sim][stack]", LoRaBasicLink, LoRaBackoffLink) {
    RxMode mode = GENERATE(RxMode::Windowed, RxMode::Continuous);
    sim::World world(20);
    EncNode<TestType> alice(world, "alice", mode, "camp", "correct horse");
    EncNode<TestType> bob(world, "bob", mode, "camp", "correct horse");
    EncNode<TestType> mallory(world, "mallory", mode, "camp", "guess");
    bool delivered = false, malloryResolved = true;

    world.spawn([&] {
        alice.rollCall.begin();
        alice.pump(8000, 3000);
        delivered = alice.messenger.sendMessage("bob", "meet at the usual place", true, 3000);
        alice.pump(40000, 3000);
    });
    world.spawn([&] { bob.rollCall.begin(); bob.pump(40000, 3000); });
    world.spawn([&] {
        mallory.rollCall.begin();
        mallory.pump(12000, 3000);
        malloryResolved = mallory.messenger.sendMessage("bob", "let me in", true, 2000);
        mallory.pump(40000, 3000);
    });
    REQUIRE(world.run(120000));
    REQUIRE(world.errors().empty());
    CHECK(delivered);
    CHECK(bob.countContent("meet at the usual place") == 1);
    CHECK(bob.inbox.size() == 1);
    CHECK_FALSE(malloryResolved);
    CHECK(mallory.inbox.empty());
    // Nobody on the real network ever learned about mallory, and vice versa.
    CHECK(alice.rollCall.getNameToIdMap().count("mallory") == 0);
    CHECK(bob.rollCall.getNameToIdMap().count("mallory") == 0);
    CHECK(mallory.rollCall.getNameToIdMap().size() == 1);
    CHECK(mallory.secure.rejectedPackets() > 0);
    CHECK(alice.rollCall.getNameToIdMap().count("bob") == 1);
    // Messages are limited by the encrypted payload size: 207 - 4.
    CHECK(alice.messenger.maxMessageLength() == 203);
}
