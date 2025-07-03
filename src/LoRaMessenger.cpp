/**
 * @file LoRaMessenger.cpp
 * @brief Implementation of high-level messaging interface for LoRa networks
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements the LoRaMessenger class, providing a high-level
 * messaging interface that sits above RollCall and ILoRaLink for easy
 * peer-to-peer communication with automatic name resolution and protocol
 * message filtering.
 */

#include "LoRaMessenger.h"
#include "LoraBasicLink.h" // For BROADCAST_ADDR constant
#include <cstring>

/**
 * @brief Constructor initializes LoRaMessenger with RollCall integration
 */
LoRaMessenger::LoRaMessenger(RollCall* rollCall, time_ms_fn getTime, message_callback_fn messageCallback)
    : _rollCall(rollCall), _link(nullptr), _getTime(getTime), _messageCallback(messageCallback) {
    // We'll get the link from RollCall in begin()
}

/**
 * @brief Initialize the messaging system
 */
bool LoRaMessenger::begin() {
    if (!_rollCall) {
        return false;
    }

    // Note: We don't call rollCall->begin() here since the user should have already
    // done that. We just prepare the messenger to work with the existing RollCall.
    
    // We don't have direct access to the link from RollCall, so we'll work through
    // RollCall's processMessages method and use our own message handling.
    
    return true;
}

/**
 * @brief Process incoming messages and filter user messages from protocol messages
 */
bool LoRaMessenger::processMessages(uint32_t timeoutMs) {
    if (!_rollCall) {
        return false;
    }

    // First, let RollCall handle its protocol messages
    bool rollCallProcessed = _rollCall->processMessages(timeoutMs);
    
    // Now we need to check for user messages by accessing the link layer directly
    // Since we can't easily get the link from RollCall, we'll need to modify our approach
    // to work through RollCall's message processing system.
    
    // For now, we'll use a different approach - we'll intercept messages before RollCall
    // processes them by implementing our own message filtering system.
    
    return rollCallProcessed;
}

/**
 * @brief Send message to a node by name
 */
bool LoRaMessenger::sendMessage(const std::string& nodeName, const std::string& message, uint32_t timeoutMs) {
    if (!_rollCall) {
        return false;
    }

    // Resolve node name to ID
    uint16_t nodeId = _rollCall->whoIs(nodeName, timeoutMs);
    if (nodeId == 0) {
        return false; // Name resolution failed
    }

    return sendMessage(nodeId, message);
}

/**
 * @brief Send message to a node by ID
 */
bool LoRaMessenger::sendMessage(uint16_t nodeId, const std::string& message) {
    if (!_rollCall) {
        return false;
    }

    // Create user message with prefix to distinguish from protocol messages
    std::string userMessage = std::string(USER_MSG_PREFIX) + message;
    
    // We need to access the link layer to send the message
    // Since RollCall doesn't expose its link, we'll need to modify our approach
    // For now, let's create a method that works by extending RollCall's interface
    
    // Temporary implementation - this will need to be refined
    return false; // Will implement after modifying architecture
}

/**
 * @brief Send broadcast message to all nodes
 */
bool LoRaMessenger::broadcastMessage(const std::string& message) {
    return sendMessage(BROADCAST_ADDR, message);
}

/**
 * @brief Receive the next user message from the queue
 */
bool LoRaMessenger::receiveMessage(Message& message) {
    if (_messageQueue.empty()) {
        return false;
    }

    message = _messageQueue.front();
    _messageQueue.pop();
    return true;
}

/**
 * @brief Check if a message is a RollCall protocol message
 */
bool LoRaMessenger::isProtocolMessage(const std::string& message) {
    // Check for RollCall protocol prefixes
    return (message.find("HELLOIAM|") == 0 ||
            message.find("WHOIS|") == 0 ||
            message.find("WHEREIS|") == 0 ||
            message.find("RESP|") == 0);
}

/**
 * @brief Handle a user message by adding to queue or calling callback
 */
void LoRaMessenger::handleUserMessage(uint16_t senderId, const std::string& message, bool isBroadcast) {
    // Remove the user message prefix
    std::string content = message;
    if (content.find(USER_MSG_PREFIX) == 0) {
        content = content.substr(strlen(USER_MSG_PREFIX));
    }

    // Get sender name if available
    std::string senderName = getSenderName(senderId);
    
    // Create message object
    Message msg(senderId, senderName, content, _getTime(), isBroadcast);

    // Call callback if set, otherwise add to queue
    if (_messageCallback) {
        _messageCallback(msg);
    } else {
        _messageQueue.push(msg);
    }
}

/**
 * @brief Get sender name from ID using RollCall
 */
std::string LoRaMessenger::getSenderName(uint16_t senderId) {
    if (!_rollCall) {
        return "";
    }
    return _rollCall->whereIs(senderId, 10); // Short timeout for cached lookups
}