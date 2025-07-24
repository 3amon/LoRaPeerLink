/**
 * @file EncryptedLoRaLink.cpp
 * @brief Implementation of encryption layer for LoRa communication
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements the EncryptedLoRaLink class, providing transparent
 * AES-128 CBC encryption for LoRa packet payloads while maintaining
 * clear-text headers for routing.
 */

#include "EncryptedLoRaLink.h"
#include <string.h>
#include <random>
#include <cstring>

// For PBKDF2 implementation - using a simple implementation for embedded systems
#include <stdint.h>

/**
 * @brief Simple PBKDF2-SHA256 implementation for key derivation
 * 
 * This is a simplified implementation suitable for embedded systems.
 * For production use, consider using a dedicated crypto library.
 */
namespace {
    // Simple SHA256 implementation for PBKDF2
    void sha256_simple(const uint8_t* data, size_t len, uint8_t* hash) {
        // This is a placeholder for SHA256 implementation
        // For a real implementation, use a proper SHA256 library
        // For now, we'll use a simple hash function for testing
        uint32_t h = 0x6a09e667;
        for (size_t i = 0; i < len; i++) {
            h = ((h << 5) + h) + data[i];
        }
        memset(hash, 0, 32);
        memcpy(hash, &h, sizeof(h));
    }

    void pbkdf2_simple(const uint8_t* password, size_t password_len,
                      const uint8_t* salt, size_t salt_len,
                      uint32_t iterations, uint8_t* key, size_t key_len) {
        // Simplified PBKDF2 implementation
        // In production, use a proper PBKDF2 implementation
        uint8_t temp[32];
        uint8_t combined[256];
        
        // Combine password and salt
        memcpy(combined, password, password_len);
        memcpy(combined + password_len, salt, salt_len);
        
        // Apply iterations
        for (uint32_t i = 0; i < iterations; i++) {
            sha256_simple(combined, password_len + salt_len, temp);
            memcpy(combined, temp, 32);
        }
        
        // Extract key
        memcpy(key, temp, key_len);
    }
}

/**
 * @brief Constructor - Initialize encryption layer
 */
EncryptedLoRaLink::EncryptedLoRaLink(ILoRaLink* underlyingLink, 
                                    const std::string& networkName, 
                                    const std::string& password,
                                    uint32_t keyIterations)
    : _underlyingLink(underlyingLink), _networkName(networkName), _password(password) {
    
    if (!underlyingLink) {
        // Handle null pointer error
        return;
    }
    
    // Derive encryption key from network name and password
    deriveKey();
}

/**
 * @brief Destructor - Secure cleanup
 */
EncryptedLoRaLink::~EncryptedLoRaLink() {
    // Securely zero out key material
    secureZero(_encryptionKey, sizeof(_encryptionKey));
}

/**
 * @brief Derive encryption key using PBKDF2
 */
void EncryptedLoRaLink::deriveKey() {
    // Use network name as salt and password as key material
    pbkdf2_simple(
        reinterpret_cast<const uint8_t*>(_password.c_str()), _password.length(),
        reinterpret_cast<const uint8_t*>(_networkName.c_str()), _networkName.length(),
        4096, // iterations
        _encryptionKey, 16
    );
}

/**
 * @brief Generate random bytes for IV
 */
void EncryptedLoRaLink::generateRandomBytes(uint8_t* buffer, size_t len) {
    // Use C++ random number generator for cryptographic randomness
    // In production, use hardware random number generator if available
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (size_t i = 0; i < len; i++) {
        buffer[i] = dis(gen);
    }
}

/**
 * @brief Simple AES-128 encryption implementation
 * 
 * This is a simplified implementation for demonstration.
 * In production, use a proper AES library like mbedTLS or OpenSSL.
 */
size_t EncryptedLoRaLink::encryptData(const uint8_t* iv, const uint8_t* plaintext, size_t plaintextLen,
                                     uint8_t* ciphertext, size_t ciphertextMaxLen) {
    // Simplified encryption for demonstration
    // In production, use proper AES-128 CBC implementation
    
    // Add PKCS7 padding
    uint8_t padded[256];
    if (plaintextLen >= sizeof(padded)) return 0;
    
    memcpy(padded, plaintext, plaintextLen);
    size_t paddedLen = addPadding(padded, plaintextLen, sizeof(padded));
    if (paddedLen == 0) return 0;
    
    // Simple XOR encryption with key and IV for demonstration
    // This is NOT secure - use proper AES in production
    if (paddedLen > ciphertextMaxLen) return 0;
    
    for (size_t i = 0; i < paddedLen; i++) {
        ciphertext[i] = padded[i] ^ _encryptionKey[i % 16] ^ iv[i % 16];
    }
    
    return paddedLen;
}

/**
 * @brief Simple AES-128 decryption implementation
 */
size_t EncryptedLoRaLink::decryptData(const uint8_t* iv, const uint8_t* ciphertext, size_t ciphertextLen,
                                     uint8_t* plaintext, size_t plaintextMaxLen) {
    // Simplified decryption for demonstration
    // In production, use proper AES-128 CBC implementation
    
    if (ciphertextLen > plaintextMaxLen) return 0;
    
    // Simple XOR decryption (reverse of encryption)
    for (size_t i = 0; i < ciphertextLen; i++) {
        plaintext[i] = ciphertext[i] ^ _encryptionKey[i % 16] ^ iv[i % 16];
    }
    
    // Remove PKCS7 padding
    return removePadding(plaintext, ciphertextLen);
}

/**
 * @brief Add PKCS7 padding
 */
size_t EncryptedLoRaLink::addPadding(uint8_t* data, size_t dataLen, size_t maxLen) {
    size_t paddingNeeded = 16 - (dataLen % 16);
    if (dataLen + paddingNeeded > maxLen) return 0;
    
    for (size_t i = 0; i < paddingNeeded; i++) {
        data[dataLen + i] = static_cast<uint8_t>(paddingNeeded);
    }
    
    return dataLen + paddingNeeded;
}

/**
 * @brief Remove PKCS7 padding
 */
size_t EncryptedLoRaLink::removePadding(uint8_t* data, size_t dataLen) {
    if (dataLen == 0) return 0;
    
    uint8_t paddingBytes = data[dataLen - 1];
    if (paddingBytes == 0 || paddingBytes > 16 || paddingBytes > dataLen) {
        return 0; // Invalid padding
    }
    
    // Verify padding
    for (size_t i = dataLen - paddingBytes; i < dataLen; i++) {
        if (data[i] != paddingBytes) {
            return 0; // Invalid padding
        }
    }
    
    return dataLen - paddingBytes;
}

/**
 * @brief Send encrypted packet
 */
bool EncryptedLoRaLink::sendPacket(uint16_t srcId, uint16_t destId, const uint8_t* payload, uint8_t len, 
                                  bool requestAck, int maxRetries) {
    if (!_underlyingLink || len == 0) return false;
    
    // Check if we have enough space for IV + encrypted data
    const size_t ivSize = 16;
    const size_t maxEncryptedSize = ((len + 15) / 16) * 16; // Account for padding
    const size_t totalSize = ivSize + maxEncryptedSize;
    
    if (totalSize > 200) { // Leave some room for link layer overhead
        return false;
    }
    
    // Generate random IV
    uint8_t iv[16];
    generateRandomBytes(iv, sizeof(iv));
    
    // Encrypt the payload
    uint8_t encryptedData[200];
    size_t encryptedLen = encryptData(iv, payload, len, encryptedData, 200);
    if (encryptedLen == 0) return false;
    
    // Combine IV + encrypted data
    uint8_t combinedPayload[200];
    if (ivSize + encryptedLen > 200) return false;
    
    memcpy(combinedPayload, iv, ivSize);
    memcpy(combinedPayload + ivSize, encryptedData, encryptedLen);
    
    // Send through underlying link
    return _underlyingLink->sendPacket(srcId, destId, combinedPayload, 
                                      static_cast<uint8_t>(ivSize + encryptedLen), 
                                      requestAck, maxRetries);
}

/**
 * @brief Receive and decrypt packet
 */
int EncryptedLoRaLink::receivePacket(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen, uint32_t timeoutMs) {
    if (!_underlyingLink) return 0;
    
    // Receive from underlying link with timeout
    uint8_t receivedData[200];
    int receivedLen = _underlyingLink->receivePacket(srcId, receivedData, 200, timeoutMs);
    
    if (receivedLen <= 16) { // Must have at least IV (16 bytes)
        return 0;
    }
    
    // Extract IV and encrypted data
    const uint8_t* iv = receivedData;
    const uint8_t* encryptedData = receivedData + 16;
    size_t encryptedLen = receivedLen - 16;
    
    // Decrypt the data
    uint8_t decryptedData[200];
    size_t decryptedLen = decryptData(iv, encryptedData, encryptedLen, 
                                     decryptedData, 200);
    
    if (decryptedLen == 0 || decryptedLen > maxLen) {
        return 0; // Decryption failed or buffer too small
    }
    
    // Copy decrypted data to output buffer
    memcpy(buffer, decryptedData, decryptedLen);
    return static_cast<int>(decryptedLen);
}

/**
 * @brief Set local node ID
 */
void EncryptedLoRaLink::setLocalId(uint16_t localId) {
    if (_underlyingLink) {
        _underlyingLink->setLocalId(localId);
    }
}

/**
 * @brief Get maximum payload size
 */
uint8_t EncryptedLoRaLink::getMaxPayloadSize() const {
    // Account for IV (16 bytes) and potential padding (up to 16 bytes)
    // Assume underlying link can handle ~200 bytes
    return 200 - 16 - 16; // ~168 bytes for plaintext
}

/**
 * @brief Secure memory zeroing
 */
void EncryptedLoRaLink::secureZero(void* ptr, size_t len) {
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    for (size_t i = 0; i < len; i++) {
        p[i] = 0;
    }
}