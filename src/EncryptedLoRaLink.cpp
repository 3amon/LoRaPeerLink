/**
 * @file EncryptedLoRaLink.cpp
 * @brief Authenticated encryption layer for LoRa links
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * See EncryptedLoRaLink.h for the scheme.
 */

#include "EncryptedLoRaLink.h"

#include <string.h>

#if defined(ESP_PLATFORM)
#if defined(__has_include)
#if __has_include(<esp_random.h>)
#include <esp_random.h>
#else
#include <esp_system.h>
#endif
#else
#include <esp_system.h>
#endif
#else
#include <random>
#endif

constexpr size_t EncryptedLoRaLink::IV_SIZE;
constexpr size_t EncryptedLoRaLink::TAG_SIZE;
constexpr uint32_t EncryptedLoRaLink::DEFAULT_KEY_ITERATIONS;

namespace {

const size_t kMaxWire = 255;

void defaultRandomBytes(uint8_t* buffer, size_t len) {
#if defined(ESP_PLATFORM)
    esp_fill_random(buffer, len);
#else
    static std::random_device device;
    for (size_t i = 0; i < len; ++i) buffer[i] = static_cast<uint8_t>(device());
#endif
}

} // namespace

EncryptedLoRaLink::EncryptedLoRaLink(ILoRaLink* underlyingLink,
                                     const std::string& networkName,
                                     const std::string& password,
                                     uint32_t keyIterations,
                                     time_ms_fn getTime,
                                     random_bytes_fn randomBytes)
    : _underlyingLink(underlyingLink), _getTime(getTime),
      _randomBytes(randomBytes ? randomBytes : &defaultRandomBytes),
      _ivCounter(0), _rejected(0) {
    uint8_t keys[32];
    lpl::pbkdf2HmacSha256(reinterpret_cast<const uint8_t*>(password.data()), password.size(),
                          reinterpret_cast<const uint8_t*>(networkName.data()), networkName.size(),
                          keyIterations == 0 ? 1 : keyIterations, keys, sizeof(keys));
    memcpy(_encKey, keys, 16);
    memcpy(_macKey, keys + 16, 16);
    _aes.setKey(_encKey);
    lpl::secureZero(keys, sizeof(keys));
}

EncryptedLoRaLink::~EncryptedLoRaLink() {
    lpl::secureZero(_encKey, sizeof(_encKey));
    lpl::secureZero(_macKey, sizeof(_macKey));
}

void EncryptedLoRaLink::exportKeysForTesting(uint8_t out[32]) const {
    memcpy(out, _encKey, 16);
    memcpy(out + 16, _macKey, 16);
}

void EncryptedLoRaLink::setLocalId(uint16_t localId) {
    if (_underlyingLink) {
        _underlyingLink->setLocalId(localId);
    }
}

uint8_t EncryptedLoRaLink::maxPayloadSize() const {
    if (!_underlyingLink) return 0;
    const size_t room = _underlyingLink->maxPayloadSize();
    if (room < IV_SIZE + 16 + TAG_SIZE) return 0;
    // The ciphertext is a whole number of blocks and always contains at least
    // one byte of padding.
    const size_t blocks = (room - IV_SIZE - TAG_SIZE) / 16;
    return static_cast<uint8_t>(blocks * 16 - 1);
}

void EncryptedLoRaLink::nextIv(uint16_t srcId, uint8_t iv[IV_SIZE]) {
    // CBC needs an IV that an attacker cannot predict. Fresh random bytes
    // would do, but random sources on small boards are sometimes weak, so
    // they are run through HMAC together with a counter: even if the random
    // source returns the same bytes every time, IVs do not repeat within one
    // power cycle and are unpredictable to anyone without the key.
    uint8_t material[16 + 8 + 2];
    _randomBytes(material, 16);
    const uint64_t counter = _ivCounter++;
    for (int i = 0; i < 8; ++i) material[16 + i] = static_cast<uint8_t>(counter >> (56 - 8 * i));
    material[24] = static_cast<uint8_t>(srcId >> 8);
    material[25] = static_cast<uint8_t>(srcId & 0xFF);

    uint8_t mac[32];
    lpl::HmacSha256 h(_macKey, sizeof(_macKey));
    const uint8_t label[2] = {'i', 'v'};
    h.update(label, sizeof(label));
    h.update(material, sizeof(material));
    h.finish(mac);
    memcpy(iv, mac, IV_SIZE);
    lpl::secureZero(material, sizeof(material));
    lpl::secureZero(mac, sizeof(mac));
}

size_t EncryptedLoRaLink::seal(uint16_t srcId, const uint8_t iv[IV_SIZE], const uint8_t* plaintext, size_t plaintextLen,
                               uint8_t* out, size_t outMax) const {
    const size_t cipherLen = (plaintextLen / 16 + 1) * 16;
    const size_t total = IV_SIZE + cipherLen + TAG_SIZE;
    if (total > outMax) return 0;

    memcpy(out, iv, IV_SIZE);
    if (lpl::aesCbcEncrypt(_aes, iv, plaintext, plaintextLen, out + IV_SIZE, cipherLen) != cipherLen) return 0;

    const uint8_t src[2] = {static_cast<uint8_t>(srcId >> 8), static_cast<uint8_t>(srcId & 0xFF)};
    uint8_t mac[32];
    lpl::HmacSha256 h(_macKey, sizeof(_macKey));
    h.update(src, sizeof(src));
    h.update(out, IV_SIZE + cipherLen);
    h.finish(mac);
    memcpy(out + IV_SIZE + cipherLen, mac, TAG_SIZE);
    return total;
}

bool EncryptedLoRaLink::open(uint16_t srcId, const uint8_t* wire, size_t wireLen,
                             uint8_t* plaintext, size_t plaintextMax, size_t& plaintextLen) const {
    plaintextLen = 0;
    if (wireLen < IV_SIZE + 16 + TAG_SIZE) return false;
    const size_t cipherLen = wireLen - IV_SIZE - TAG_SIZE;
    if (cipherLen % 16 != 0) return false;

    // Authenticate first. Nothing is decrypted unless the tag is right.
    const uint8_t src[2] = {static_cast<uint8_t>(srcId >> 8), static_cast<uint8_t>(srcId & 0xFF)};
    uint8_t mac[32];
    lpl::HmacSha256 h(_macKey, sizeof(_macKey));
    h.update(src, sizeof(src));
    h.update(wire, IV_SIZE + cipherLen);
    h.finish(mac);
    if (!lpl::constantTimeEqual(mac, wire + IV_SIZE + cipherLen, TAG_SIZE)) return false;

    uint8_t decrypted[kMaxWire];
    if (cipherLen > sizeof(decrypted)) return false;
    bool ok = false;
    const size_t n = lpl::aesCbcDecrypt(_aes, wire, wire + IV_SIZE, cipherLen, decrypted, sizeof(decrypted), ok);
    bool delivered = false;
    if (ok && n <= plaintextMax) {
        if (n > 0) memcpy(plaintext, decrypted, n);
        plaintextLen = n;
        delivered = true;
    }
    lpl::secureZero(decrypted, sizeof(decrypted));
    return delivered;
}

bool EncryptedLoRaLink::sendPacket(uint16_t srcId, uint16_t destId, const uint8_t* payload, uint8_t len,
                                   bool requestAck, int maxRetries) {
    if (!_underlyingLink || payload == nullptr || len == 0) return false;
    if (len > maxPayloadSize()) return false;

    uint8_t iv[IV_SIZE];
    nextIv(srcId, iv);

    uint8_t wire[kMaxWire];
    const size_t wireLen = seal(srcId, iv, payload, len, wire, _underlyingLink->maxPayloadSize());
    if (wireLen == 0) return false;

    return _underlyingLink->sendPacket(srcId, destId, wire, static_cast<uint8_t>(wireLen), requestAck, maxRetries);
}

int EncryptedLoRaLink::receivePacket(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen, uint32_t timeoutMs) {
    if (!_underlyingLink || srcId == nullptr || buffer == nullptr) return 0;

    const uint32_t start = _getTime ? _getTime() : 0;
    uint32_t remaining = timeoutMs;

    for (;;) {
        uint8_t wire[kMaxWire];
        uint16_t src = 0;
        const int wireLen = _underlyingLink->receivePacket(&src, wire, static_cast<uint8_t>(kMaxWire), remaining);
        if (wireLen <= 0) return 0;

        size_t plainLen = 0;
        if (open(src, wire, static_cast<size_t>(wireLen), buffer, maxLen, plainLen) && plainLen > 0) {
            *srcId = src;
            return static_cast<int>(plainLen);
        }
        ++_rejected;

        // Not one of ours. Without a clock we cannot tell how much of the
        // timeout is left, so return; with one, keep listening.
        if (!_getTime) return 0;
        const uint32_t elapsed = _getTime() - start;
        if (elapsed >= timeoutMs) return 0;
        remaining = timeoutMs - elapsed;
    }
}
