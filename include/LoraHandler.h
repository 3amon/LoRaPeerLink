/**
 * @file LoraHandler.h
 * @brief High-level LoRa communication handler for Arduino applications
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file defines the LoraHandler class, which provides a high-level interface
 * for LoRa communication in Arduino-based applications. It handles:
 * - Message encryption and encoding
 * - Chunked message transmission for large payloads
 * - Message integrity verification using SHA256
 * - Integration with display systems for status updates
 * 
 * The handler supports both simple ping messages and complex encrypted
 * data transmission with automatic chunking for messages exceeding
 * LoRa packet size limits.
 */

#ifndef T3_LORA
#define T3_LORA

#include "Arduino.h"
#include "WiFi.h"
#include <Wire.h>  
#include "HT_SSD1306Wire.h"
#include <map>
#include <SHA256.h>  // Include Arduino SHA256 library
#include <Base64.h>  // Include Arduino Base64 library
#include <ScreenHandler.h>
#include <AES.h>
#include <Base64.h>
#include <AesHandler.hpp>

/**
 * @class LoraHandler
 * @brief High-level LoRa communication manager for Arduino applications
 * 
 * This class provides a comprehensive LoRa communication system that handles:
 * 
 * **Key Features:**
 * - **Message Encryption**: AES encryption for secure communication
 * - **Data Integrity**: SHA256 checksums for message verification
 * - **Chunked Transmission**: Automatic splitting of large messages
 * - **Base64 Encoding**: Safe transmission of binary data
 * - **Display Integration**: Status updates on connected screens
 * - **Ping Functionality**: Network connectivity testing
 * 
 * **Message Flow:**
 * 1. Messages are encrypted using AES encryption
 * 2. Encrypted data is base64 encoded for transmission
 * 3. Large messages are split into chunks with sequence numbers
 * 4. Each chunk includes checksum for integrity verification
 * 5. Received chunks are reassembled and verified
 * 
 * **Usage Pattern:**
 * 1. Create LoraHandler instance
 * 2. Call setup() to initialize communication
 * 3. Use send_msg() to transmit messages
 * 4. Call loop() regularly to process incoming messages
 * 5. Check newMessageReceived() for incoming data
 * 
 * @par Example:
 * @code
 * LoraHandler lora;
 * lora.setup();
 * 
 * // In main loop:
 * lora.loop();
 * if (lora.newMessageReceived()) {
 *     Serial.println("Received: " + lora.last_message_rcv);
 * }
 * 
 * // Send a message:
 * lora.send_msg("Hello World");
 * @endcode
 */
class LoraHandler {

    public:
        /**
         * @brief Constructor initializes the LoRa handler
         * 
         * Creates a new LoraHandler instance and initializes internal
         * state variables for message processing and communication.
         */
        LoraHandler();
        
        /**
         * @brief Initialize the LoRa communication system
         * 
         * Sets up the LoRa radio, encryption systems, and display integration.
         * This method must be called before using other LoraHandler functionality.
         * Configures:
         * - LoRa radio parameters
         * - Encryption keys and algorithms
         * - Display system integration
         * - Network identification
         */
        void setup();
        
        /**
         * @brief Main processing loop for LoRa communication
         * 
         * Handles incoming messages, processes received chunks, and manages
         * communication state. This method should be called regularly from
         * the main Arduino loop to ensure proper message processing.
         * 
         * Functions performed:
         * - Check for incoming LoRa packets
         * - Process received message chunks
         * - Reassemble multi-chunk messages
         * - Verify message integrity
         * - Update display status
         */
        void loop();
        
        /**
         * @brief Check if a new complete message has been received
         * @return true if a new message is available in last_message_rcv
         * 
         * Indicates whether a complete message has been received and verified.
         * When this returns true, the message content is available in the
         * last_message_rcv member variable.
         */
        bool newMessageReceived();
        
        /**
         * @brief Send a ping message to test network connectivity
         * 
         * Transmits a small test message to verify LoRa communication is
         * working properly. Useful for network diagnostics and range testing.
         * The ping message includes node identification and timestamp.
         */
        void send_ping();
        
        /**
         * @brief Send a text message over LoRa
         * @param msg Message content to transmit
         * 
         * Encrypts and transmits the specified message using the configured
         * encryption and chunking systems. Large messages are automatically
         * split into multiple chunks for transmission.
         * 
         * Process:
         * 1. Encrypt message using AES
         * 2. Base64 encode encrypted data
         * 3. Split into chunks if necessary
         * 4. Add checksums and sequence numbers
         * 5. Transmit chunks via LoRa
         */
        void send_msg(String msg);
        
        String last_message_rcv;    ///< Last complete message received
        String current_send_msg;    ///< Current message being sent

    private:
        std::map<int, String> receivedChunks;  ///< Buffer for reassembling chunked messages
        String last_rcv_checksum;              ///< Checksum of last received message
        bool new_msg_rcv;                      ///< Flag indicating new message available
        String ping_msg;                       ///< Template for ping messages
        String chip_id;                        ///< Unique identifier for this device

        /**
         * @brief Calculate SHA256 hash of data for integrity verification
         * @param data Input data to hash
         * @return SHA256 hash as hexadecimal string
         * 
         * Computes SHA256 checksum for message integrity verification.
         * Used to detect transmission errors and ensure data integrity.
         */
        String calculateSHA256(const String& data);
        
        /**
         * @brief Decode base64 encoded string
         * @param encoded Base64 encoded input string
         * @return Decoded binary data as string
         * 
         * Decodes base64 encoded data received over LoRa communication.
         * Used to recover binary encrypted data from text transmission.
         */
        String base64Decode(const String& encoded);
        
        /**
         * @brief Encode binary data as base64 string
         * @param message Binary data to encode
         * @return Base64 encoded string
         * 
         * Encodes binary data for safe transmission over LoRa.
         * Ensures encrypted data can be transmitted as text without corruption.
         */
        String base64Encode(const String& message);
        
        /**
         * @brief Process incoming LoRa message
         * @param message Raw message received from LoRa radio
         * 
         * Handles initial processing of received messages including:
         * - Format validation
         * - Chunk identification
         * - Duplicate detection
         * - Routing to appropriate handler
         */
        void receiveMessage(const String& message);
        
        /**
         * @brief Process and decode received message chunks
         * @param message Message chunk to process
         * 
         * Handles chunked message reassembly:
         * - Extracts chunk sequence numbers
         * - Stores chunks in reassembly buffer
         * - Detects when all chunks received
         * - Reassembles complete messages
         * - Verifies message integrity
         */
        void processMessage(const String& message);
        
        /**
         * @brief Verify integrity of reassembled message
         * @return true if message passes integrity checks
         * 
         * Validates reassembled messages using checksums and other
         * integrity verification methods to ensure data was received
         * correctly and completely.
         */
        bool verifyFileIntegrity();
};

#endif // T3_LORA