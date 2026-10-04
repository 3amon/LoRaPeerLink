/**
 * @file EncryptedLoRaLink.h
 * @brief Authenticated encryption layer for LoRa links
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * EncryptedLoRaLink wraps any ILoRaLink and encrypts and authenticates the
 * payloads that pass through it. The link header (addresses, sequence
 * number, flags) stays in the clear so that routing, acknowledgments and
 * address filtering keep working.
 *
 * Scheme (encrypt-then-MAC):
 *
 *   keys  = PBKDF2-HMAC-SHA256(password, salt = network name, iterations, 32 bytes)
 *           encryption key = first 16 bytes, MAC key = last 16 bytes
 *   C     = AES-128-CBC(encryption key, IV, PKCS#7(plaintext))
 *   tag   = first 8 bytes of HMAC-SHA256(MAC key, source ID (2 bytes, big-endian) || IV || C)
 *   wire  = IV (16) || C (16..) || tag (8)
 *
 * A receiver verifies the tag before it decrypts anything. Packets from a
 * different network, with a wrong password, corrupted in transit or forged
 * with a different source ID are dropped silently.
 *
 * What this does not provide:
 *   - Replay protection. A recorded packet can be sent again later and will
 *     be accepted. Add a counter or timestamp to the payload if that matters.
 *   - Forward secrecy. Everyone who knows the password can read all traffic,
 *     past and future.
 *   - Hiding who talks to whom, or how much: headers and lengths are visible.
 *
 * Version 2 is not compatible with version 1 of this class, which only
 * obfuscated the payload and must not be used.
 */

#ifndef ENCRYPTED_LORA_LINK_H
#define ENCRYPTED_LORA_LINK_H

#include "ILoRaLink.h"
#include "LplCrypto.h"

#include <stddef.h>
#include <stdint.h>
#include <string>

class EncryptedLoRaLink : public ILoRaLink {
public:
    static constexpr size_t IV_SIZE = 16;
    static constexpr size_t TAG_SIZE = 8;
    static constexpr uint32_t DEFAULT_KEY_ITERATIONS = 4096;

    /**
     * Source of random bytes for initialisation vectors. On real hardware
     * pass a function backed by a hardware random number generator.
     */
    using random_bytes_fn = void (*)(uint8_t* buffer, size_t len);

    /**
     * @param underlyingLink Link to wrap (must outlive this object)
     * @param networkName    Shared network name (used as the key derivation salt)
     * @param password       Shared secret
     * @param keyIterations  PBKDF2 iteration count; every node must use the same value
     * @param getTime        Optional millisecond clock. With it, receivePacket()
     *                       keeps listening for its full timeout after
     *                       rejecting a packet; without it the call returns.
     * @param randomBytes    Optional random source (see random_bytes_fn)
     *
     * Key derivation runs in the constructor and takes a noticeable time on a
     * microcontroller (4096 iterations is roughly 16000 SHA-256 block
     * operations).
     */
    EncryptedLoRaLink(ILoRaLink* underlyingLink,
                      const std::string& networkName,
                      const std::string& password,
                      uint32_t keyIterations = DEFAULT_KEY_ITERATIONS,
                      time_ms_fn getTime = nullptr,
                      random_bytes_fn randomBytes = nullptr);

    ~EncryptedLoRaLink();

    EncryptedLoRaLink(const EncryptedLoRaLink&) = delete;
    EncryptedLoRaLink& operator=(const EncryptedLoRaLink&) = delete;

    /**
     * @brief Encrypt, authenticate and send a payload
     * @return false if the payload is empty or longer than maxPayloadSize(),
     *         or if the underlying link reports failure
     */
    bool sendPacket(uint16_t srcId, uint16_t destId, const uint8_t* payload, uint8_t len,
                    bool requestAck = false, int maxRetries = 3) override;

    /**
     * @brief Receive, verify and decrypt a payload
     * @return Plaintext length, or 0 if nothing valid arrived
     *
     * Note that the link layer acknowledges a packet before this layer has
     * checked it, so an acknowledgment means "received", not "authentic".
     */
    int receivePacket(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen, uint32_t timeoutMs = 1000) override;

    void setLocalId(uint16_t localId) override;

    /** Largest plaintext that fits in one packet of the underlying link (207 bytes on a standard link). */
    uint8_t maxPayloadSize() const override;

    /** Same as maxPayloadSize(); kept for source compatibility with version 1. */
    uint8_t getMaxPayloadSize() const { return maxPayloadSize(); }

    /** Packets rejected because the tag, the length or the padding was wrong. */
    uint32_t rejectedPackets() const { return _rejected; }

    /**
     * @brief Encrypt one payload into its wire format (exposed for tests and tools)
     * @param iv 16 byte initialisation vector to use
     * @return Number of bytes written to @p out, 0 if @p outMax is too small
     */
    size_t seal(uint16_t srcId, const uint8_t iv[IV_SIZE], const uint8_t* plaintext, size_t plaintextLen,
                uint8_t* out, size_t outMax) const;

    /**
     * @brief Verify and decrypt one wire format payload
     * @return true if the tag and padding were valid
     */
    bool open(uint16_t srcId, const uint8_t* wire, size_t wireLen,
              uint8_t* plaintext, size_t plaintextMax, size_t& plaintextLen) const;

    /** Copy of the derived key material (32 bytes), for cross-validation tools only. */
    void exportKeysForTesting(uint8_t out[32]) const;

private:
    void nextIv(uint16_t srcId, uint8_t iv[IV_SIZE]);

    ILoRaLink* _underlyingLink;
    time_ms_fn _getTime;
    random_bytes_fn _randomBytes;
    lpl::Aes128 _aes;
    uint8_t _macKey[16];
    uint8_t _encKey[16];
    uint64_t _ivCounter;
    uint32_t _rejected;
};

#endif // ENCRYPTED_LORA_LINK_H
