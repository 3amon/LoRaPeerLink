/**
 * @file LoRaMessenger.h
 * @brief High-level messaging interface for LoRa peer-to-peer networks
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements a high-level messaging layer that sits above RollCall
 * and ILoRaLink, providing an intuitive interface for sending and receiving
 * user messages while automatically handling node name resolution and
 * filtering out internal protocol messages.
 */

#ifndef LORA_MESSENGER_H
#define LORA_MESSENGER_H

#include "RollCall.h"
#include "ILoRaLink.h"

#include <stdint.h>
#include <string>
#include <vector>
#include <queue>

/**
 * @struct Message
 * @brief Represents a received message with metadata
 */
struct Message {
    uint16_t senderId;          ///< Node ID of sender
    std::string senderName;     ///< Node name of sender (if known)
    std::string content;        ///< Message content
    uint32_t timestamp;         ///< When message was received (from getTime function)
    bool isBroadcast;           ///< true if this was a broadcast message
    
    Message() : senderId(0), timestamp(0), isBroadcast(false) {}
    Message(uint16_t id, const std::string& name, const std::string& msg, uint32_t time, bool broadcast)
        : senderId(id), senderName(name), content(msg), timestamp(time), isBroadcast(broadcast) {}
};

/**
 * @class LoRaMessenger
 * @brief High-level messaging interface for LoRa peer-to-peer networks
 * 
 * This class provides the primary interface most users of the LoRaPeerLink library
 * will use. It sits above RollCall and the link layer to provide simple message
 * sending and receiving capabilities.
 * 
 * **Key Features:**
 * - **Send by Node Name**: Send messages using human-readable node names
 * - **Send by Node ID**: Send messages using numeric node IDs
 * - **Broadcast Messages**: Send messages to all nodes in the network
 * - **Message Reception**: Receive messages addressed to this node or broadcasts
 * - **Protocol Filtering**: Automatically filters out RollCall protocol messages
 * - **Name Resolution**: Integrates with RollCall for automatic name lookup
 * 
 * **Usage Pattern:**
 * 1. Initialize with RollCall instance (which provides link layer access)
 * 2. Call begin() to start the messaging system
 * 3. Use sendMessage() methods to send messages
 * 4. Regularly call processMessages() to handle incoming messages
 * 5. Use receiveMessage() or message callbacks to get received messages
 * 
 * @par Example:
 * @code
 * // Setup link and naming layers
 * LoRaBasicLink link(&radio, getTime, sleep);
 * RollCall rollCall(&link, "sensor-1", getTime, sleep);
 * LoRaMessenger messenger(&rollCall, getTime);
 * 
 * // Initialize the system
 * rollCall.begin();
 * messenger.begin();
 * 
 * // In main loop:
 * messenger.processMessages();
 * 
 * // Send messages
 * messenger.sendMessage("gateway-main", "Hello from sensor!");
 * messenger.broadcastMessage("Status: All systems OK");
 * 
 * // Receive messages
 * Message msg;
 * if (messenger.receiveMessage(msg)) {
 *     printf("Received from %s: %s\n", msg.senderName.c_str(), msg.content.c_str());
 * }
 * @endcode
 */
class LoRaMessenger {
public:
    using time_ms_fn = uint32_t (*)();
    using message_callback_fn = void (*)(const Message& message);

    /**
     * Constructor
     * @param rollCall Pointer to RollCall instance for name resolution
     * @param getTime Function to get current time in milliseconds
     * @param messageCallback Optional callback for received messages
     */
    LoRaMessenger(RollCall* rollCall, time_ms_fn getTime, message_callback_fn messageCallback = nullptr);

    /**
     * Initialize the messaging system
     * @return true if initialization successful
     */
    bool begin();

    /**
     * Process incoming messages and handle protocol traffic
     * Should be called regularly in the main loop
     * @param timeoutMs Maximum time to wait for messages
     * @return true if any messages were processed
     */
    bool processMessages(uint32_t timeoutMs = 100);

    /**
     * Send message to a specific node by name
     * @param nodeName Target node name
     * @param message Message content to send
     * @param timeoutMs Timeout for name resolution if needed
     * @return true if message sent successfully
     */
    bool sendMessage(const std::string& nodeName, const std::string& message, uint32_t timeoutMs = 1000);

    /**
     * Send message to a specific node by ID
     * @param nodeId Target node ID
     * @param message Message content to send
     * @return true if message sent successfully
     */
    bool sendMessage(uint16_t nodeId, const std::string& message);

    /**
     * Send broadcast message to all nodes
     * @param message Message content to send
     * @return true if message sent successfully
     */
    bool broadcastMessage(const std::string& message);

    /**
     * Receive the next available user message
     * @param message Reference to Message struct to fill
     * @return true if a message was available
     */
    bool receiveMessage(Message& message);

    /**
     * Check if there are pending user messages
     * @return Number of pending messages
     */
    size_t getMessageCount() const { return _messageQueue.size(); }

    /**
     * Get the associated RollCall instance
     * @return Pointer to RollCall instance
     */
    RollCall* getRollCall() const { return _rollCall; }

    /**
     * Set message callback function
     * @param callback Function to call when messages are received
     */
    void setMessageCallback(message_callback_fn callback) { _messageCallback = callback; }

private:
    RollCall* _rollCall;
    ILoRaLink* _link; // Retrieved from RollCall
    time_ms_fn _getTime;
    message_callback_fn _messageCallback;

    std::queue<Message> _messageQueue;
    
    // Protocol constants for message identification
    static constexpr const char* USER_MSG_PREFIX = "USRMSG|";

    /**
     * Check if a message is a RollCall protocol message
     * @param message Message content to check
     * @return true if it's a protocol message that should be filtered
     */
    bool isProtocolMessage(const std::string& message);

    /**
     * Process a user message and add to queue or call callback
     * @param senderId Sender node ID
     * @param message Message content
     * @param isBroadcast Whether this was a broadcast message
     */
    void handleUserMessage(uint16_t senderId, const std::string& message, bool isBroadcast);

    /**
     * Get sender name from ID using RollCall
     * @param senderId Node ID to look up
     * @return Node name if known, empty string otherwise
     */
    std::string getSenderName(uint16_t senderId);
};

#endif // LORA_MESSENGER_H