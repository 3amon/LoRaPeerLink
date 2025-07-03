/**
 * @file PeerMessenger.h
 * @brief High-level peer-to-peer messaging interface for LoRa networks
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file defines the PeerMessenger class, which provides a user-friendly
 * interface for sending and receiving messages in a LoRa peer-to-peer network.
 * It sits above the RollCall layer and handles application-level messaging
 * while automatically managing node discovery and name resolution.
 */

#ifndef PEER_MESSENGER_H
#define PEER_MESSENGER_H

#include "RollCall.h"
#include <string>
#include <memory>

/**
 * @struct UserMessage
 * @brief Structure representing a received user message
 */
struct UserMessage {
    uint16_t srcId;           ///< Source node ID
    std::string srcName;      ///< Source node name (if known)
    std::string content;      ///< Message content
};

/**
 * @class PeerMessenger
 * @brief High-level interface for peer-to-peer messaging
 * 
 * PeerMessenger provides an intuitive API for sending and receiving messages
 * in a LoRa peer-to-peer network. It automatically handles:
 * 
 * **Key Features:**
 * - **Send by Name**: Send messages to nodes by their human-readable names
 * - **Send by ID**: Send messages directly to node IDs for efficiency
 * - **Broadcast Messages**: Send messages to all nodes in the network
 * - **Message Reception**: Receive messages addressed to this node or broadcasts
 * - **Automatic Discovery**: Leverages RollCall for seamless node discovery
 * - **Protocol Isolation**: Filters out discovery messages from user messages
 * 
 * **Usage Pattern:**
 * 1. Initialize with RollCall instance and call begin()
 * 2. Regularly call processMessages() to handle network traffic
 * 3. Use sendMessage() variants to send messages
 * 4. Use hasMessage()/receiveMessage() to handle incoming messages
 * 
 * @par Example:
 * @code
 * LoRaBasicLink link(&radio, getTime, sleep);
 * RollCall rollCall(&link, "sensor-1", getTime, sleep);
 * PeerMessenger messenger(&rollCall);
 * 
 * rollCall.begin();
 * messenger.begin();
 * 
 * // In main loop:
 * messenger.processMessages();
 * 
 * // Send messages:
 * messenger.sendMessage("gateway-main", "Temperature: 25.3C");
 * messenger.broadcastMessage("System startup complete");
 * 
 * // Receive messages:
 * if (messenger.hasMessage()) {
 *     UserMessage msg = messenger.receiveMessage();
 *     Serial.println("From " + msg.srcName + ": " + msg.content);
 * }
 * @endcode
 */
class PeerMessenger {
public:
    using log_fn = void (*)(const char*);

    /**
     * Constructor
     * @param rollCall Pointer to initialized RollCall instance for name resolution
     * @param logMessage Function to log debug messages (optional, for debugging only)
     */
    PeerMessenger(RollCall* rollCall, log_fn logMessage = nullptr);

    /**
     * Initialize the PeerMessenger
     * Must be called after RollCall::begin()
     * @return true if initialization successful
     */
    bool begin();

    /**
     * Process incoming messages and handle network traffic
     * Should be called regularly in the main loop
     * @param timeoutMs Maximum time to wait for messages
     * @return true if any message was processed
     */
    bool processMessages(uint32_t timeoutMs = 100);

    /**
     * Send message to a specific node by ID
     * @param destId Destination node ID
     * @param message Message content to send
     * @param requestAck Whether to request acknowledgment (default: false)
     * @return true if message was sent successfully
     */
    bool sendMessage(uint16_t destId, const std::string& message, bool requestAck = false);

    /**
     * Send message to a specific node by name
     * @param destName Destination node name
     * @param message Message content to send
     * @param requestAck Whether to request acknowledgment (default: false)
     * @param timeoutMs Maximum time to wait for name resolution (default: 1000ms)
     * @return true if message was sent successfully
     */
    bool sendMessage(const std::string& destName, const std::string& message, 
                    bool requestAck = false, uint32_t timeoutMs = 1000);

    /**
     * Broadcast message to all nodes in the network
     * @param message Message content to broadcast
     * @return true if message was sent successfully
     */
    bool broadcastMessage(const std::string& message);

    /**
     * Check if any user messages are available
     * @return true if messages are waiting to be received
     */
    bool hasMessage() const;

    /**
     * Receive the next available user message
     * @return UserMessage structure with sender info and content
     * @note Call hasMessage() first to check availability
     */
    UserMessage receiveMessage();

    /**
     * Get the number of queued messages waiting to be received
     * @return Number of messages in receive queue
     */
    size_t getMessageCount() const;

    /**
     * Get access to the underlying RollCall instance
     * @return Reference to RollCall instance
     */
    RollCall& getRollCall() { return *_rollCall; }

    /**
     * Default console logging function for debugging
     * @param message Message to log to console
     */
    static void consoleLog(const char* message);

private:
    RollCall* _rollCall;              ///< RollCall instance for name resolution
    log_fn _logMessage;               ///< Optional logging function

    // Protocol constants
    static constexpr const char* MESSAGE_PREFIX = "MSG|";
    static constexpr const char* HELLOIAM_PREFIX = "HELLOIAM|";
    static constexpr const char* WHOIS_PREFIX = "WHOIS|";
    static constexpr const char* WHEREIS_PREFIX = "WHEREIS|";
    static constexpr const char* RESPONSE_PREFIX = "RESP|";
};

#endif // PEER_MESSENGER_H