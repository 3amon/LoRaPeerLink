// Cryptographic primitives checked against published test vectors.
// The expected values were also generated independently with Python
// (hashlib and the "cryptography" package); see validate_encryption.py.
#include <catch2/catch_test_macros.hpp>

#include "LplCrypto.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

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

std::string sha256Hex(const std::string& text) {
    uint8_t d[32];
    lpl::sha256(reinterpret_cast<const uint8_t*>(text.data()), text.size(), d);
    return hex(d, 32);
}

} // namespace

TEST_CASE("Crypto: AES-128 block cipher (FIPS 197 appendix C.1)", "[crypto][aes]") {
    auto key = unhex("000102030405060708090a0b0c0d0e0f");
    auto pt = unhex("00112233445566778899aabbccddeeff");
    lpl::Aes128 aes(key.data());
    uint8_t ct[16], back[16];
    aes.encryptBlock(pt.data(), ct);
    CHECK(hex(ct, 16) == "69c4e0d86a7b0430d8cdb78070b4c55a");
    aes.decryptBlock(ct, back);
    CHECK(hex(back, 16) == "00112233445566778899aabbccddeeff");

    // In-place operation is allowed.
    memcpy(back, pt.data(), 16);
    aes.encryptBlock(back, back);
    CHECK(hex(back, 16) == "69c4e0d86a7b0430d8cdb78070b4c55a");
    aes.decryptBlock(back, back);
    CHECK(hex(back, 16) == "00112233445566778899aabbccddeeff");
}

TEST_CASE("Crypto: AES-128 ECB known answers (NIST SP 800-38A F.1.1)", "[crypto][aes]") {
    auto key = unhex("2b7e151628aed2a6abf7158809cf4f3c");
    lpl::Aes128 aes(key.data());
    const char* vectors[4][2] = {
        {"6bc1bee22e409f96e93d7e117393172a", "3ad77bb40d7a3660a89ecaf32466ef97"},
        {"ae2d8a571e03ac9c9eb76fac45af8e51", "f5d3d58503b9699de785895a96fdbaaf"},
        {"30c81c46a35ce411e5fbc1191a0a52ef", "43b1cd7f598ece23881b00e3ed030688"},
        {"f69f2445df4f9b17ad2b417be66c3710", "7b0c785e27e8ad3f8223207104725dd4"}};
    for (auto& v : vectors) {
        uint8_t out[16];
        aes.encryptBlock(unhex(v[0]).data(), out);
        CHECK(hex(out, 16) == v[1]);
        aes.decryptBlock(unhex(v[1]).data(), out);
        CHECK(hex(out, 16) == v[0]);
    }
}

TEST_CASE("Crypto: AES-128 decrypt inverts encrypt for random keys and blocks", "[crypto][aes]") {
    std::mt19937 rng(2024);
    for (int i = 0; i < 2000; ++i) {
        uint8_t key[16], pt[16], ct[16], back[16];
        for (auto& b : key) b = static_cast<uint8_t>(rng());
        for (auto& b : pt) b = static_cast<uint8_t>(rng());
        lpl::Aes128 aes(key);
        aes.encryptBlock(pt, ct);
        aes.decryptBlock(ct, back);
        REQUIRE(memcmp(pt, back, 16) == 0);
        REQUIRE(memcmp(pt, ct, 16) != 0);
    }
}

TEST_CASE("Crypto: AES-128-CBC (NIST SP 800-38A F.2.1) with PKCS#7 padding", "[crypto][aes][cbc]") {
    auto key = unhex("2b7e151628aed2a6abf7158809cf4f3c");
    auto iv = unhex("000102030405060708090a0b0c0d0e0f");
    auto pt = unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
    lpl::Aes128 aes(key.data());

    uint8_t ct[64];
    size_t n = lpl::aesCbcEncrypt(aes, iv.data(), pt.data(), pt.size(), ct, sizeof(ct));
    REQUIRE(n == 48);   // two data blocks plus one full block of padding
    CHECK(hex(ct, 32) == "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");

    uint8_t back[64];
    bool ok = false;
    size_t m = lpl::aesCbcDecrypt(aes, iv.data(), ct, n, back, sizeof(back), ok);
    CHECK(ok);
    REQUIRE(m == 32);
    CHECK(memcmp(back, pt.data(), 32) == 0);
}

TEST_CASE("Crypto: CBC round trip for every length, and padding is checked", "[crypto][cbc]") {
    uint8_t key[16], iv[16];
    for (int i = 0; i < 16; ++i) { key[i] = static_cast<uint8_t>(i * 3 + 1); iv[i] = static_cast<uint8_t>(200 - i); }
    lpl::Aes128 aes(key);
    uint8_t pt[100], ct[128], back[128];
    for (int i = 0; i < 100; ++i) pt[i] = static_cast<uint8_t>(i ^ 0x5A);

    for (size_t len = 0; len <= 100; ++len) {
        INFO("length " << len);
        size_t n = lpl::aesCbcEncrypt(aes, iv, pt, len, ct, sizeof(ct));
        REQUIRE(n == (len / 16 + 1) * 16);
        bool ok = false;
        size_t m = lpl::aesCbcDecrypt(aes, iv, ct, n, back, sizeof(back), ok);
        REQUIRE(ok);
        REQUIRE(m == len);
        REQUIRE(memcmp(back, pt, len) == 0);
    }

    // Output buffer too small.
    CHECK(lpl::aesCbcEncrypt(aes, iv, pt, 16, ct, 31) == 0);
    CHECK(lpl::aesCbcEncrypt(aes, iv, pt, 15, ct, 16) == 16);

    // Bad lengths and bad padding are reported.
    bool ok = true;
    lpl::aesCbcDecrypt(aes, iv, ct, 0, back, sizeof(back), ok);
    CHECK_FALSE(ok);
    ok = true;
    lpl::aesCbcDecrypt(aes, iv, ct, 17, back, sizeof(back), ok);
    CHECK_FALSE(ok);
    ok = true;
    lpl::aesCbcDecrypt(aes, iv, ct, 32, back, 16, ok);
    CHECK_FALSE(ok);

    // A block whose plaintext does not end in valid padding.
    int rejected = 0;
    for (int trial = 0; trial < 256; ++trial) {
        uint8_t raw[16] = {0};
        raw[15] = static_cast<uint8_t>(trial);     // final byte = claimed padding length
        raw[14] = 0x77;                            // but the byte before it is not padding
        uint8_t block[16], x[16];
        for (int i = 0; i < 16; ++i) x[i] = static_cast<uint8_t>(raw[i] ^ iv[i]);
        aes.encryptBlock(x, block);
        ok = true;
        lpl::aesCbcDecrypt(aes, iv, block, 16, back, sizeof(back), ok);
        if (!ok) ++rejected;
    }
    CHECK(rejected == 255);    // only a final byte of 0x01 is valid padding here
}

TEST_CASE("Crypto: SHA-256 (FIPS 180-4 examples)", "[crypto][sha256]") {
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha256Hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("Crypto: SHA-256 gives the same digest however the input is split", "[crypto][sha256]") {
    std::string data;
    for (int i = 0; i < 300; ++i) data.push_back(static_cast<char>(i * 31 + 7));
    uint8_t whole[32];
    lpl::sha256(reinterpret_cast<const uint8_t*>(data.data()), data.size(), whole);

    // Every message length around the block boundaries, fed in odd-sized pieces.
    for (size_t len : {0u, 1u, 55u, 56u, 57u, 63u, 64u, 65u, 119u, 120u, 127u, 128u, 129u, 300u}) {
        uint8_t expected[32];
        lpl::sha256(reinterpret_cast<const uint8_t*>(data.data()), len, expected);
        for (size_t chunk : {1u, 3u, 7u, 63u, 64u, 65u}) {
            lpl::Sha256 h;
            for (size_t off = 0; off < len; off += chunk) {
                h.update(reinterpret_cast<const uint8_t*>(data.data()) + off, std::min(chunk, len - off));
            }
            uint8_t got[32];
            h.finish(got);
            INFO("length " << len << ", chunk " << chunk);
            REQUIRE(memcmp(got, expected, 32) == 0);
        }
    }
}

TEST_CASE("Crypto: HMAC-SHA256 (RFC 4231)", "[crypto][hmac]") {
    uint8_t mac[32];
    std::vector<uint8_t> key(20, 0x0b);
    lpl::hmacSha256(key.data(), key.size(), reinterpret_cast<const uint8_t*>("Hi There"), 8, mac);
    CHECK(hex(mac, 32) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    lpl::hmacSha256(reinterpret_cast<const uint8_t*>("Jefe"), 4,
                    reinterpret_cast<const uint8_t*>("what do ya want for nothing?"), 28, mac);
    CHECK(hex(mac, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    // Test case 6: key longer than the block size is hashed first.
    std::vector<uint8_t> longKey(131, 0xaa);
    const std::string text = "Test Using Larger Than Block-Size Key - Hash Key First";
    lpl::hmacSha256(longKey.data(), longKey.size(), reinterpret_cast<const uint8_t*>(text.data()), text.size(), mac);
    CHECK(hex(mac, 32) == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

    // Incremental interface gives the same answer.
    lpl::HmacSha256 h(reinterpret_cast<const uint8_t*>("Jefe"), 4);
    h.update(reinterpret_cast<const uint8_t*>("what do ya want "), 16);
    h.update(reinterpret_cast<const uint8_t*>("for nothing?"), 12);
    h.finish(mac);
    CHECK(hex(mac, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST_CASE("Crypto: PBKDF2-HMAC-SHA256 (RFC 7914 section 11 and common vectors)", "[crypto][pbkdf2]") {
    uint8_t out[40];
    auto derive = [&](const std::string& pw, const std::string& salt, uint32_t iterations, size_t len) {
        lpl::pbkdf2HmacSha256(reinterpret_cast<const uint8_t*>(pw.data()), pw.size(),
                              reinterpret_cast<const uint8_t*>(salt.data()), salt.size(), iterations, out, len);
        return hex(out, len);
    };
    CHECK(derive("password", "salt", 1, 32) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK(derive("password", "salt", 2, 32) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    CHECK(derive("password", "salt", 4096, 32) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    // Output longer than one hash block, long password and salt.
    CHECK(derive("passwordPASSWORDpassword", "saltSALTsaltSALTsaltSALTsaltSALTsalt", 4096, 40) ==
          "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9");
    // Zero iterations is treated as one rather than producing garbage.
    CHECK(derive("password", "salt", 0, 32) == derive("password", "salt", 1, 32));
    // Inputs far longer than any internal buffer.
    const std::string huge(5000, 'p');
    CHECK(derive(huge, huge, 2, 16).size() == 32);
}

TEST_CASE("Crypto: constant time comparison and zeroing", "[crypto]") {
    uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(lpl::constantTimeEqual(a, b, 8));
    b[7] ^= 1;
    CHECK_FALSE(lpl::constantTimeEqual(a, b, 8));
    b[7] ^= 1;
    b[0] ^= 0x80;
    CHECK_FALSE(lpl::constantTimeEqual(a, b, 8));
    CHECK(lpl::constantTimeEqual(a, b, 0));
    lpl::secureZero(a, sizeof(a));
    for (uint8_t v : a) CHECK(v == 0);
}
