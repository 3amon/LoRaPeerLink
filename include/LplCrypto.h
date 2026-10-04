/**
 * @file LplCrypto.h
 * @brief Small, dependency-free cryptographic primitives for LoRaPeerLink
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * AES-128, SHA-256, HMAC-SHA256 and PBKDF2-HMAC-SHA256, written in portable
 * C++ so the library builds on any microcontroller without pulling in a
 * crypto dependency. The implementations are checked against the published
 * test vectors (FIPS 197, NIST SP 800-38A, FIPS 180-4, RFC 4231, RFC 7914)
 * and against Python's "cryptography" package; see tests/test_crypto.cpp and
 * validate_encryption.py.
 *
 * The AES implementation is table based and is not hardened against
 * cache-timing side channels. That is acceptable for a microcontroller that
 * runs nothing else; do not reuse it on a shared multi-tenant machine.
 */

#ifndef LPL_CRYPTO_H
#define LPL_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

namespace lpl {

/** AES-128 block cipher. */
class Aes128 {
public:
    static constexpr size_t BLOCK_SIZE = 16;
    static constexpr size_t KEY_SIZE = 16;

    Aes128() : _roundKeys() {}
    explicit Aes128(const uint8_t key[KEY_SIZE]) : _roundKeys() { setKey(key); }
    ~Aes128();

    void setKey(const uint8_t key[KEY_SIZE]);
    void encryptBlock(const uint8_t in[BLOCK_SIZE], uint8_t out[BLOCK_SIZE]) const;
    void decryptBlock(const uint8_t in[BLOCK_SIZE], uint8_t out[BLOCK_SIZE]) const;

private:
    uint8_t _roundKeys[176];
};

/**
 * AES-128-CBC with PKCS#7 padding.
 * @param outMax Size of @p out; needs room for plaintextLen rounded up to the next multiple of 16 (always at least one byte of padding)
 * @return Ciphertext length, or 0 if @p out is too small
 */
size_t aesCbcEncrypt(const Aes128& aes, const uint8_t iv[16], const uint8_t* plaintext, size_t plaintextLen,
                     uint8_t* out, size_t outMax);

/**
 * Inverse of aesCbcEncrypt().
 * @param[out] ok Set to false if the length or the padding is invalid
 * @return Plaintext length (may legitimately be 0)
 */
size_t aesCbcDecrypt(const Aes128& aes, const uint8_t iv[16], const uint8_t* ciphertext, size_t ciphertextLen,
                     uint8_t* out, size_t outMax, bool& ok);

/** Incremental SHA-256. */
class Sha256 {
public:
    static constexpr size_t DIGEST_SIZE = 32;
    static constexpr size_t BLOCK_SIZE = 64;

    Sha256() { reset(); }
    void reset();
    void update(const uint8_t* data, size_t len);
    void finish(uint8_t digest[DIGEST_SIZE]);

private:
    void compress(const uint8_t block[BLOCK_SIZE]);

    uint32_t _state[8];
    uint64_t _totalLen;
    uint8_t _buffer[BLOCK_SIZE];
    size_t _bufferLen;
};

void sha256(const uint8_t* data, size_t len, uint8_t digest[32]);

/** Incremental HMAC-SHA256 (RFC 2104). */
class HmacSha256 {
public:
    HmacSha256(const uint8_t* key, size_t keyLen);
    ~HmacSha256();
    void update(const uint8_t* data, size_t len);
    void finish(uint8_t mac[32]);

private:
    Sha256 _inner;
    uint8_t _outerPad[Sha256::BLOCK_SIZE];
};

void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t mac[32]);

/** PBKDF2 with HMAC-SHA256 (RFC 8018). Any password, salt and output length. */
void pbkdf2HmacSha256(const uint8_t* password, size_t passwordLen,
                      const uint8_t* salt, size_t saltLen,
                      uint32_t iterations, uint8_t* out, size_t outLen);

/** Compare two buffers without leaking, through timing, where they differ. */
bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t len);

/** Overwrite memory in a way the compiler will not optimise away. */
void secureZero(void* ptr, size_t len);

} // namespace lpl

#endif // LPL_CRYPTO_H
