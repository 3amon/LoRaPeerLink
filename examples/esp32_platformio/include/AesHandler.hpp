/**
 * @file AesHandler.hpp
 * @brief AES encryption/decryption handler for secure LoRa communication
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file defines the AESHandler class, which provides AES-128 encryption
 * and decryption capabilities for securing LoRa communications. It includes:
 * - AES-128 ECB mode encryption/decryption
 * - PKCS7 padding for block alignment
 * - Base64 encoding for safe text transmission
 * - Simple interface for message security
 */

#include <Arduino.h>
#include <AES.h>
#include <Base64.h>

/**
 * @class AESHandler
 * @brief AES-128 encryption and decryption handler for secure communications
 * 
 * This class provides a simple interface for AES-128 encryption and decryption
 * operations commonly needed in secure LoRa communication systems. It handles:
 * 
 * **Encryption Process:**
 * 1. Apply PKCS7 padding to align message to 16-byte blocks
 * 2. Encrypt using AES-128 in ECB mode
 * 3. Encode result as Base64 for safe text transmission
 * 
 * **Decryption Process:**
 * 1. Decode Base64 encoded message
 * 2. Decrypt using AES-128 in ECB mode
 * 3. Remove PKCS7 padding to recover original message
 * 
 * **Security Notes:**
 * - Uses AES-128 ECB mode (suitable for small messages)
 * - Key must be exactly 16 bytes for AES-128
 * - ECB mode should only be used for small, independent messages
 * - For higher security, consider CBC or GCM modes for larger systems
 * 
 * @par Example:
 * @code
 * uint8_t key[16] = {0x01, 0x02, 0x03, ...}; // 16-byte key
 * AESHandler aes(key);
 * 
 * String plaintext = "Secret message";
 * String encrypted = aes.encrypt(plaintext);
 * String decrypted = aes.decrypt(encrypted);
 * // decrypted == plaintext
 * @endcode
 */
class AESHandler {
public:
    /**
     * @brief Constructor initializes AES with the provided key
     * @param key Pointer to 16-byte AES-128 encryption key
     * 
     * Sets up the AES encryption system with the specified key.
     * The key must be exactly 16 bytes for AES-128 operation.
     * Store the key securely and ensure it's synchronized between
     * communicating devices.
     */
    AESHandler(const uint8_t* key) {
        aes.setKey(key, 16);  // Initialize with 16-byte AES-128 key
    }

    /**
     * @brief Encrypt a string using AES-128 ECB mode with PKCS7 padding
     * @param plain_text String to encrypt
     * @return Base64-encoded encrypted string
     * 
     * Encrypts the input string using AES-128 in ECB mode:
     * 1. Applies PKCS7 padding to ensure message length is multiple of 16 bytes
     * 2. Encrypts each 16-byte block using AES-128
     * 3. Encodes the result as Base64 for safe text transmission
     * 
     * The resulting string is safe to transmit over text-based protocols
     * like LoRa without risk of character encoding issues.
     */
    String encrypt(const String& plain_text) {
        String padded_text = plain_text;
        padMessage(padded_text);

        size_t length = padded_text.length();
        uint8_t encrypted[length];
        uint8_t buffer[16];

        // Encrypt each 16-byte block
        for (size_t i = 0; i < length; i += 16) {
            aes.encryptBlock(buffer, (uint8_t*)(padded_text.substring(i, i + 16).c_str()));
            memcpy(encrypted + i, buffer, 16);
        }

        // Encode as Base64 for safe transmission
        int encodedLength = Base64.encodedLength(length);
        char encoded[encodedLength + 1];
        Base64.encode(encoded, (char*)encrypted, length);
        return String(encoded);
    }

    /**
     * @brief Decrypt a base64-encoded string using AES-128 ECB mode
     * @param encrypted_text Base64-encoded encrypted string
     * @return Decrypted plaintext string
     * 
     * Decrypts a previously encrypted message:
     * 1. Decodes the Base64 encoded data to binary
     * 2. Decrypts each 16-byte block using AES-128
     * 3. Removes PKCS7 padding to recover original message
     * 
     * The input must be a valid Base64-encoded AES-encrypted message
     * created with the same key. Invalid input may result in
     * corrupted output or decryption failure.
     */
    String decrypt(const String& encrypted_text) {
        // Decode Base64 to binary data
        int decodedLength = Base64.decodedLength((char*)encrypted_text.c_str(), encrypted_text.length());
        uint8_t decoded[decodedLength];
        Base64.decode((char*)decoded, (char*)encrypted_text.c_str(), encrypted_text.length());

        uint8_t decrypted[decodedLength];
        uint8_t buffer[16];

        // Decrypt each 16-byte block
        for (int i = 0; i < decodedLength; i += 16) {
            aes.decryptBlock(buffer, decoded + i);
            memcpy(decrypted + i, buffer, 16);
        }

        // Convert to string and remove padding
        String decrypted_text((char*)decrypted, decodedLength);
        removePadding(decrypted_text);
        return decrypted_text;
    }

private:
    AES128 aes;  ///< AES-128 encryption object

    /**
     * @brief Apply PKCS7 padding to message for block alignment
     * @param message Message to pad (modified in place)
     * 
     * Adds PKCS7 padding to ensure the message length is a multiple
     * of 16 bytes (AES block size). The padding value indicates how
     * many padding bytes were added, enabling proper removal during
     * decryption.
     */
    void padMessage(String& message) {
        size_t padding_needed = 16 - (message.length() % 16);
        char padding_char = padding_needed;

        for (size_t i = 0; i < padding_needed; i++) {
            message += padding_char;
        }
    }

    /**
     * @brief Remove PKCS7 padding from decrypted message
     * @param message Message to unpad (modified in place)
     * 
     * Removes PKCS7 padding from a decrypted message by examining
     * the last byte to determine how many padding bytes to remove.
     * Validates padding to prevent corruption from invalid data.
     */
    void removePadding(String& message) {
        if (message.length() == 0) return;

        char padding_char = message[message.length() - 1];
        size_t padding = static_cast<size_t>(padding_char);

        // Validate padding value and remove if valid
        if (padding > 0 && padding <= 16) {
            message.remove(message.length() - padding);
        }
    }
};