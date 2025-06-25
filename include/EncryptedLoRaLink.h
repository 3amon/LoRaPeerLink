/**
 * @file EncryptedLoRaLink.h
 * @brief Encryption layer for LoRa communication providing AES-128 CBC encryption
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements an encryption layer that wraps any ILoRaLink implementation
 * to provide transparent encryption of payload data while keeping headers in clear
 * text for routing. The implementation provides:
 * - AES-128 CBC encryption with random IV per packet
 * - Key derivation from Network Name and Password using PBKDF2
 * - Transparent encryption/decryption for applications
 * - Maintains packet routing information in clear text
 * - Backward compatibility with unencrypted nodes (configurable)
 */

#ifndef ENCRYPTED_LORA_LINK_H
#define ENCRYPTED_LORA_LINK_H

#include "ILoRaLink.h"
#include <stdint.h>
#include <string>
#include <memory>

/**
 * @class EncryptedLoRaLink
 * @brief Encryption wrapper for LoRa link layer implementations
 * 
 * This class implements an encryption layer that sits between applications and
 * the underlying LoRa link implementation. It provides transparent encryption
 * of packet payloads while maintaining clear-text headers for routing.
 * 
 * **Key Features:**
 * - **AES-128 CBC Encryption**: Secure encryption with random IV per packet
 * - **Key Derivation**: PBKDF2 with configurable iterations from network name/password
 * - **Header Preservation**: Routing information remains unencrypted
 * - **Transparent Operation**: Applications don't need to handle encryption
 * - **Backward Compatibility**: Can optionally work with unencrypted nodes
 * 
 * **Security Design:**
 * - Network Name serves as salt for key derivation
 * - Password provides the secret key material
 * - Random IV prepended to each encrypted packet
 * - PBKDF2 with 4096 iterations (configurable) for key strengthening
 * 
 * **Packet Structure:**
 * ```
 * [Clear Header][IV (16 bytes)][Encrypted Payload][CRC]
 * ```
 * 
 * The underlying link layer handles all headers and CRC, this layer only
 * processes the payload portion by prepending IV and encrypting the data.
 * 
 * @par Usage Example:
 * @code
 * // Create base link layer
 * SemtechRadio radio(915000000);
 * LoRaBasicLink baseLink(&radio, millis, delay);
 * 
 * // Wrap with encryption
 * auto encryptedLink = std::make_unique<EncryptedLoRaLink>(
 *     &baseLink, "MyNetwork", "SecurePassword123"
 * );
 * 
 * // Use transparently - encryption is automatic
 * std::string message = "Secret Data";
 * encryptedLink->sendPacket(1, 2, (uint8_t*)message.c_str(), message.length());
 * @endcode
 */
class EncryptedLoRaLink : public ILoRaLink {
public:
    /**
     * @brief Constructor - creates encrypted wrapper around existing link
     * @param underlyingLink Pointer to ILoRaLink implementation to wrap
     * @param networkName Name of the network (used as salt for key derivation)
     * @param password Password for encryption (used for key derivation)
     * @param keyIterations Number of PBKDF2 iterations for key derivation (default: 4096)
     * 
     * Initializes the encryption layer with the specified network credentials.
     * The networkName and password are used together to derive a unique
     * encryption key via PBKDF2. Higher iteration counts provide better
     * security but require more computation time.
     */
    EncryptedLoRaLink(ILoRaLink* underlyingLink, 
                      const std::string& networkName, 
                      const std::string& password,
                      uint32_t keyIterations = 4096);

    /**
     * @brief Destructor - ensures secure cleanup of key material
     */
    ~EncryptedLoRaLink();

    /**
     * @brief Send encrypted packet through underlying link
     * @param srcId Source node ID
     * @param destId Destination node ID (0xFFFF for broadcast)
     * @param payload Data to encrypt and transmit
     * @param len Length of payload data
     * @param requestAck Whether to request acknowledgment
     * @param maxRetries Maximum retry attempts
     * @return true if packet sent successfully
     * 
     * Encrypts the payload data and sends through the underlying link:
     * 1. Generate random IV (16 bytes)
     * 2. Encrypt payload using AES-128 CBC with generated IV
     * 3. Prepend IV to encrypted data
     * 4. Send combined [IV + encrypted data] as payload to underlying link
     */
    bool sendPacket(uint16_t srcId, uint16_t destId, const uint8_t* payload, uint8_t len, 
                   bool requestAck = false, int maxRetries = 3) override;

    /**
     * @brief Receive and decrypt packet from underlying link
     * @param srcId Pointer to store source node ID
     * @param buffer Buffer to store decrypted payload
     * @param maxLen Maximum buffer size
     * @return Length of decrypted payload, 0 if no packet or decryption failed
     * 
     * Receives encrypted packet from underlying link and decrypts:
     * 1. Receive packet from underlying link (includes IV + encrypted data)
     * 2. Extract IV from first 16 bytes of payload
     * 3. Decrypt remaining data using AES-128 CBC with extracted IV
     * 4. Return decrypted payload to application
     */
    int receivePacket(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen) override;

    /**
     * @brief Set local node ID (passed through to underlying link)
     * @param localId Node ID for this device
     */
    void setLocalId(uint16_t localId) override;

    /**
     * @brief Get maximum payload size accounting for encryption overhead
     * @return Maximum plaintext payload size that can be encrypted and transmitted
     * 
     * Calculates the maximum payload size considering:
     * - IV overhead (16 bytes)
     * - AES block padding (up to 16 bytes)
     * - Underlying link limitations
     */
    uint8_t getMaxPayloadSize() const;

private:
    ILoRaLink* _underlyingLink;     ///< Wrapped link layer implementation
    uint8_t _encryptionKey[16];     ///< AES-128 encryption key (derived from network credentials)
    std::string _networkName;       ///< Network name (used as salt)
    std::string _password;          ///< Password (used for key derivation)

    /**
     * @brief Derive encryption key from network name and password
     * 
     * Uses PBKDF2-SHA256 to derive a 16-byte AES key from the network
     * name (as salt) and password. This provides key strengthening and
     * ensures different networks with the same password have different keys.
     */
    void deriveKey();

    /**
     * @brief Generate cryptographically secure random bytes
     * @param buffer Buffer to fill with random data
     * @param len Number of random bytes to generate
     * 
     * Generates secure random data for IV generation. Uses the best
     * available random source on the platform.
     */
    void generateRandomBytes(uint8_t* buffer, size_t len);

    /**
     * @brief Encrypt data using AES-128 CBC mode
     * @param iv Initialization vector (16 bytes)
     * @param plaintext Data to encrypt
     * @param plaintextLen Length of plaintext data
     * @param ciphertext Buffer for encrypted output
     * @param ciphertextMaxLen Maximum size of ciphertext buffer
     * @return Length of encrypted data (includes padding)
     */
    size_t encryptData(const uint8_t* iv, const uint8_t* plaintext, size_t plaintextLen,
                      uint8_t* ciphertext, size_t ciphertextMaxLen);

    /**
     * @brief Decrypt data using AES-128 CBC mode
     * @param iv Initialization vector (16 bytes)
     * @param ciphertext Encrypted data to decrypt
     * @param ciphertextLen Length of encrypted data
     * @param plaintext Buffer for decrypted output
     * @param plaintextMaxLen Maximum size of plaintext buffer
     * @return Length of decrypted data (padding removed), 0 on error
     */
    size_t decryptData(const uint8_t* iv, const uint8_t* ciphertext, size_t ciphertextLen,
                      uint8_t* plaintext, size_t plaintextMaxLen);

    /**
     * @brief Apply PKCS7 padding to data
     * @param data Data to pad (modified in place)
     * @param dataLen Current length of data
     * @param maxLen Maximum buffer size
     * @return New length after padding, 0 if buffer too small
     */
    size_t addPadding(uint8_t* data, size_t dataLen, size_t maxLen);

    /**
     * @brief Remove PKCS7 padding from data
     * @param data Data to unpad (modified in place)
     * @param dataLen Current length of padded data
     * @return New length after removing padding, 0 if invalid padding
     */
    size_t removePadding(uint8_t* data, size_t dataLen);

    /**
     * @brief Secure memory zeroing (prevents compiler optimization)
     * @param ptr Pointer to memory to zero
     * @param len Number of bytes to zero
     */
    void secureZero(void* ptr, size_t len);
};

#endif // ENCRYPTED_LORA_LINK_H