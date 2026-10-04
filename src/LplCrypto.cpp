/**
 * @file LplCrypto.cpp
 * @brief AES-128, SHA-256, HMAC-SHA256 and PBKDF2 for LoRaPeerLink
 * @author LoRaPeerLink Project
 * @version 2.0
 */

#include "LplCrypto.h"

#include <string.h>

namespace lpl {

constexpr size_t Aes128::BLOCK_SIZE;
constexpr size_t Aes128::KEY_SIZE;
constexpr size_t Sha256::DIGEST_SIZE;
constexpr size_t Sha256::BLOCK_SIZE;

// ---------------------------------------------------------------------------
// AES-128 (FIPS 197)
// ---------------------------------------------------------------------------

namespace {

const uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

const uint8_t kInvSbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d};

const uint8_t kRcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

inline uint8_t xtime(uint8_t x) {
    return static_cast<uint8_t>((x << 1) ^ ((x >> 7) * 0x1b));
}

// Multiplication in GF(2^8).
inline uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    while (b) {
        if (b & 1) result ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return result;
}

inline void addRoundKey(uint8_t state[16], const uint8_t* roundKey) {
    for (int i = 0; i < 16; ++i) state[i] ^= roundKey[i];
}

} // namespace

Aes128::~Aes128() {
    secureZero(_roundKeys, sizeof(_roundKeys));
}

void Aes128::setKey(const uint8_t key[KEY_SIZE]) {
    memcpy(_roundKeys, key, 16);
    for (int i = 4; i < 44; ++i) {
        uint8_t t[4];
        memcpy(t, &_roundKeys[(i - 1) * 4], 4);
        if (i % 4 == 0) {
            const uint8_t first = t[0];
            t[0] = static_cast<uint8_t>(kSbox[t[1]] ^ kRcon[i / 4 - 1]);
            t[1] = kSbox[t[2]];
            t[2] = kSbox[t[3]];
            t[3] = kSbox[first];
        }
        for (int j = 0; j < 4; ++j) {
            _roundKeys[i * 4 + j] = static_cast<uint8_t>(_roundKeys[(i - 4) * 4 + j] ^ t[j]);
        }
    }
}

void Aes128::encryptBlock(const uint8_t in[BLOCK_SIZE], uint8_t out[BLOCK_SIZE]) const {
    uint8_t s[16];
    memcpy(s, in, 16);
    addRoundKey(s, _roundKeys);

    for (int round = 1; round <= 10; ++round) {
        // SubBytes
        for (int i = 0; i < 16; ++i) s[i] = kSbox[s[i]];

        // ShiftRows (state is column major: s[row + 4 * column])
        uint8_t t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;

        // MixColumns (not in the last round)
        if (round != 10) {
            for (int c = 0; c < 4; ++c) {
                uint8_t* col = &s[c * 4];
                const uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                const uint8_t all = static_cast<uint8_t>(a0 ^ a1 ^ a2 ^ a3);
                col[0] = static_cast<uint8_t>(a0 ^ all ^ xtime(static_cast<uint8_t>(a0 ^ a1)));
                col[1] = static_cast<uint8_t>(a1 ^ all ^ xtime(static_cast<uint8_t>(a1 ^ a2)));
                col[2] = static_cast<uint8_t>(a2 ^ all ^ xtime(static_cast<uint8_t>(a2 ^ a3)));
                col[3] = static_cast<uint8_t>(a3 ^ all ^ xtime(static_cast<uint8_t>(a3 ^ a0)));
            }
        }

        addRoundKey(s, &_roundKeys[round * 16]);
    }
    memcpy(out, s, 16);
    secureZero(s, sizeof(s));
}

void Aes128::decryptBlock(const uint8_t in[BLOCK_SIZE], uint8_t out[BLOCK_SIZE]) const {
    uint8_t s[16];
    memcpy(s, in, 16);
    addRoundKey(s, &_roundKeys[160]);

    for (int round = 9; round >= 0; --round) {
        // InvShiftRows
        uint8_t t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;

        // InvSubBytes
        for (int i = 0; i < 16; ++i) s[i] = kInvSbox[s[i]];

        addRoundKey(s, &_roundKeys[round * 16]);

        // InvMixColumns (not after the final AddRoundKey)
        if (round != 0) {
            for (int c = 0; c < 4; ++c) {
                uint8_t* col = &s[c * 4];
                const uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = static_cast<uint8_t>(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
                col[1] = static_cast<uint8_t>(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
                col[2] = static_cast<uint8_t>(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
                col[3] = static_cast<uint8_t>(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
            }
        }
    }
    memcpy(out, s, 16);
    secureZero(s, sizeof(s));
}

size_t aesCbcEncrypt(const Aes128& aes, const uint8_t iv[16], const uint8_t* plaintext, size_t plaintextLen,
                     uint8_t* out, size_t outMax) {
    const size_t padded = (plaintextLen / 16 + 1) * 16;
    if (padded > outMax) return 0;
    const uint8_t pad = static_cast<uint8_t>(padded - plaintextLen);

    uint8_t chain[16];
    memcpy(chain, iv, 16);
    for (size_t offset = 0; offset < padded; offset += 16) {
        uint8_t block[16];
        for (size_t i = 0; i < 16; ++i) {
            const size_t pos = offset + i;
            const uint8_t byte = pos < plaintextLen ? plaintext[pos] : pad;
            block[i] = static_cast<uint8_t>(byte ^ chain[i]);
        }
        aes.encryptBlock(block, chain);
        memcpy(out + offset, chain, 16);
        secureZero(block, sizeof(block));
    }
    return padded;
}

size_t aesCbcDecrypt(const Aes128& aes, const uint8_t iv[16], const uint8_t* ciphertext, size_t ciphertextLen,
                     uint8_t* out, size_t outMax, bool& ok) {
    ok = false;
    if (ciphertextLen == 0 || ciphertextLen % 16 != 0 || ciphertextLen > outMax) return 0;

    uint8_t chain[16];
    memcpy(chain, iv, 16);
    for (size_t offset = 0; offset < ciphertextLen; offset += 16) {
        uint8_t block[16];
        aes.decryptBlock(ciphertext + offset, block);
        for (size_t i = 0; i < 16; ++i) out[offset + i] = static_cast<uint8_t>(block[i] ^ chain[i]);
        memcpy(chain, ciphertext + offset, 16);
        secureZero(block, sizeof(block));
    }

    // PKCS#7: the last byte says how many padding bytes there are (1..16),
    // and every padding byte has that value.
    const uint8_t pad = out[ciphertextLen - 1];
    if (pad == 0 || pad > 16) return 0;
    uint8_t diff = 0;
    for (size_t i = ciphertextLen - pad; i < ciphertextLen; ++i) diff |= static_cast<uint8_t>(out[i] ^ pad);
    if (diff != 0) return 0;

    ok = true;
    return ciphertextLen - pad;
}

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
// ---------------------------------------------------------------------------

namespace {

const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

} // namespace

void Sha256::reset() {
    _state[0] = 0x6a09e667; _state[1] = 0xbb67ae85; _state[2] = 0x3c6ef372; _state[3] = 0xa54ff53a;
    _state[4] = 0x510e527f; _state[5] = 0x9b05688c; _state[6] = 0x1f83d9ab; _state[7] = 0x5be0cd19;
    _totalLen = 0;
    _bufferLen = 0;
}

void Sha256::compress(const uint8_t block[BLOCK_SIZE]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = _state[0], b = _state[1], c = _state[2], d = _state[3];
    uint32_t e = _state[4], f = _state[5], g = _state[6], h = _state[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + kSha256K[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    _state[0] += a; _state[1] += b; _state[2] += c; _state[3] += d;
    _state[4] += e; _state[5] += f; _state[6] += g; _state[7] += h;
}

void Sha256::update(const uint8_t* data, size_t len) {
    _totalLen += len;
    while (len > 0) {
        const size_t take = (BLOCK_SIZE - _bufferLen < len) ? BLOCK_SIZE - _bufferLen : len;
        memcpy(_buffer + _bufferLen, data, take);
        _bufferLen += take;
        data += take;
        len -= take;
        if (_bufferLen == BLOCK_SIZE) {
            compress(_buffer);
            _bufferLen = 0;
        }
    }
}

void Sha256::finish(uint8_t digest[DIGEST_SIZE]) {
    const uint64_t bitLen = _totalLen * 8;
    _buffer[_bufferLen++] = 0x80;
    if (_bufferLen > 56) {
        memset(_buffer + _bufferLen, 0, BLOCK_SIZE - _bufferLen);
        compress(_buffer);
        _bufferLen = 0;
    }
    memset(_buffer + _bufferLen, 0, 56 - _bufferLen);
    for (int i = 0; i < 8; ++i) _buffer[56 + i] = static_cast<uint8_t>(bitLen >> (56 - 8 * i));
    compress(_buffer);

    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<uint8_t>(_state[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(_state[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(_state[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(_state[i]);
    }
    secureZero(_buffer, sizeof(_buffer));
    reset();
}

void sha256(const uint8_t* data, size_t len, uint8_t digest[32]) {
    Sha256 h;
    h.update(data, len);
    h.finish(digest);
}

// ---------------------------------------------------------------------------
// HMAC-SHA256 (RFC 2104) and PBKDF2 (RFC 8018)
// ---------------------------------------------------------------------------

HmacSha256::HmacSha256(const uint8_t* key, size_t keyLen) {
    uint8_t block[Sha256::BLOCK_SIZE];
    memset(block, 0, sizeof(block));
    if (keyLen > Sha256::BLOCK_SIZE) {
        sha256(key, keyLen, block);     // Long keys are hashed first
    } else if (keyLen > 0) {
        memcpy(block, key, keyLen);
    }

    uint8_t innerPad[Sha256::BLOCK_SIZE];
    for (size_t i = 0; i < Sha256::BLOCK_SIZE; ++i) {
        innerPad[i] = static_cast<uint8_t>(block[i] ^ 0x36);
        _outerPad[i] = static_cast<uint8_t>(block[i] ^ 0x5c);
    }
    _inner.update(innerPad, sizeof(innerPad));
    secureZero(block, sizeof(block));
    secureZero(innerPad, sizeof(innerPad));
}

HmacSha256::~HmacSha256() {
    secureZero(_outerPad, sizeof(_outerPad));
}

void HmacSha256::update(const uint8_t* data, size_t len) {
    _inner.update(data, len);
}

void HmacSha256::finish(uint8_t mac[32]) {
    uint8_t innerDigest[32];
    _inner.finish(innerDigest);
    Sha256 outer;
    outer.update(_outerPad, sizeof(_outerPad));
    outer.update(innerDigest, sizeof(innerDigest));
    outer.finish(mac);
    secureZero(innerDigest, sizeof(innerDigest));
}

void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t mac[32]) {
    HmacSha256 h(key, keyLen);
    h.update(data, dataLen);
    h.finish(mac);
}

void pbkdf2HmacSha256(const uint8_t* password, size_t passwordLen,
                      const uint8_t* salt, size_t saltLen,
                      uint32_t iterations, uint8_t* out, size_t outLen) {
    if (iterations == 0) iterations = 1;
    uint32_t blockIndex = 1;
    while (outLen > 0) {
        uint8_t u[32];
        uint8_t t[32];
        const uint8_t counter[4] = {static_cast<uint8_t>(blockIndex >> 24), static_cast<uint8_t>(blockIndex >> 16),
                                    static_cast<uint8_t>(blockIndex >> 8), static_cast<uint8_t>(blockIndex)};
        {
            HmacSha256 h(password, passwordLen);
            h.update(salt, saltLen);
            h.update(counter, sizeof(counter));
            h.finish(u);
        }
        memcpy(t, u, 32);
        for (uint32_t i = 1; i < iterations; ++i) {
            hmacSha256(password, passwordLen, u, 32, u);
            for (int k = 0; k < 32; ++k) t[k] ^= u[k];
        }
        const size_t take = outLen < 32 ? outLen : 32;
        memcpy(out, t, take);
        out += take;
        outLen -= take;
        ++blockIndex;
        secureZero(u, sizeof(u));
        secureZero(t, sizeof(t));
    }
}

bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

void secureZero(void* ptr, size_t len) {
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    for (size_t i = 0; i < len; ++i) p[i] = 0;
}

} // namespace lpl
