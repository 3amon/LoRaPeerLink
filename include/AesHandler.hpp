#include <Arduino.h>
#include <AES.h>
#include <Base64.h>

class AESHandler {
public:
    // Constructor that sets the AES key
    AESHandler(const uint8_t* key) {
        aes.setKey(key, 16);  // Initialize with 16-byte AES-128 key
    }

    // Encrypt a String using AES-128 ECB mode with PKCS7 padding
    String encrypt(const String& plain_text) {
        String padded_text = plain_text;
        padMessage(padded_text);

        size_t length = padded_text.length();
        uint8_t encrypted[length];
        uint8_t buffer[16];

        for (size_t i = 0; i < length; i += 16) {
            aes.encryptBlock(buffer, (uint8_t*)(padded_text.substring(i, i + 16).c_str()));
            memcpy(encrypted + i, buffer, 16);
        }

        int encodedLength = Base64.encodedLength(length);
        char encoded[encodedLength + 1];
        Base64.encode(encoded, (char*)encrypted, length);
        return String(encoded);
    }

    // Decrypt a base64-encoded String using AES-128 ECB mode with PKCS7 unpadding
    String decrypt(const String& encrypted_text) {
        int decodedLength = Base64.decodedLength((char*)encrypted_text.c_str(), encrypted_text.length());
        uint8_t decoded[decodedLength];
        Base64.decode((char*)decoded, (char*)encrypted_text.c_str(), encrypted_text.length());

        uint8_t decrypted[decodedLength];
        uint8_t buffer[16];

        for (int i = 0; i < decodedLength; i += 16) {
            aes.decryptBlock(buffer, decoded + i);
            memcpy(decrypted + i, buffer, 16);
        }

        String decrypted_text((char*)decrypted, decodedLength);
        removePadding(decrypted_text);
        return decrypted_text;
    }

private:
    AES128 aes;

    // Function to pad a String message using PKCS7 padding
    void padMessage(String& message) {
        size_t padding_needed = 16 - (message.length() % 16);
        char padding_char = padding_needed;

        for (size_t i = 0; i < padding_needed; i++) {
            message += padding_char;
        }
    }

    // Function to remove PKCS7 padding
    void removePadding(String& message) {
        if (message.length() == 0) return;

        char padding_char = message[message.length() - 1];
        size_t padding = static_cast<size_t>(padding_char);

        if (padding > 0 && padding <= 16) {
            message.remove(message.length() - padding);
        }
    }
};