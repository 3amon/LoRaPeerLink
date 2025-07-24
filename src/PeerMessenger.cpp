/**
 * @file PeerMessenger.cpp
 * @brief Implementation of high-level peer-to-peer messaging interface
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements the PeerMessenger class, providing a user-friendly
 * interface for sending and receiving messages in LoRa peer-to-peer networks.
 * It manages application-level messaging while delegating node discovery
 * and network management to the RollCall layer.
 */

#include "PeerMessenger.h"
#include "LoraBasicLink.h"  // For MAX_PAYLOAD constant
#include <cstring>
#include <cstdio>  // For printf

PeerMessenger::PeerMessenger(RollCall* rollCall, log_fn logMessage)
    : _rollCall(rollCall), _logMessage(logMessage) {
    if (!_rollCall) {
        if (_logMessage) {
            _logMessage("[PeerMessenger] Error: RollCall instance is null");
        }
        return;
    }
}

bool PeerMessenger::begin() {
    if (!_rollCall) {
        return false;
    }

    if (_logMessage) {
        std::string logMsg = "[PeerMessenger] Initialized for node: " + _rollCall->getNodeName() + 
                           " (ID: " + std::to_string(_rollCall->getNodeId()) + ")";
        _logMessage(logMsg.c_str());
    }

    return true;
}

bool PeerMessenger::processMessages(uint32_t timeoutMs) {
    if (!_rollCall) {
        return false;
    }
    
    // Get access to the underlying link
    uint16_t srcId;
    uint8_t buffer[MAX_PAYLOAD];
    
    // Try to receive a packet with the specified timeout
    int len = _rollCall->getLink().receivePacket(&srcId, buffer, MAX_PAYLOAD, timeoutMs);
    if (len <= 0) {
        return false;
    }
    
    // Convert buffer to string
    std::string message(reinterpret_cast<const char*>(buffer), len);
    
    // Check if this is a RollCall message
    if (_rollCall->isRollCallMessage(message)) {
        // Let RollCall process it
        return _rollCall->processRollCallMessage(message, srcId);
    }
    
    // Check if this is a user message
    if (message.find(MESSAGE_PREFIX) == 0) {
        // Handle user message
        size_t prefixLen = strlen(MESSAGE_PREFIX);
        std::string content = message.substr(prefixLen);
        
        // Queue the user message
        UserMessage_Internal userMsg;
        userMsg.srcId = srcId;
        userMsg.content = content;
        _userMessageQueue.push(userMsg);
        
        // Log the received user message if logging is enabled
        if (_logMessage) {
            std::string logMsg = "[PeerMessenger] Received user message from ID " + std::to_string(srcId) + 
                               ": " + content;
            _logMessage(logMsg.c_str());
        }
        
        return true;
    }
    
    // Unknown message type
    return false;
}

bool PeerMessenger::sendMessage(uint16_t destId, const std::string& message, bool requestAck) {
    if (!_rollCall) {
        return false;
    }

    // Log the outgoing message if logging is enabled
    if (_logMessage) {
        std::string logMsg = "[PeerMessenger] Sending to ID " + std::to_string(destId) + 
                           ": " + message;
        _logMessage(logMsg.c_str());
    }
    
    // Send through our own user message capability
    return sendUserMessage(destId, message, requestAck);
}

bool PeerMessenger::sendMessage(const std::string& destName, const std::string& message, 
                               bool requestAck, uint32_t timeoutMs) {
    if (!_rollCall) {
        return false;
    }

    // Resolve name to ID using RollCall
    uint16_t destId = _rollCall->whoIs(destName, timeoutMs);
    if (destId == 0) {
        if (_logMessage) {
            std::string logMsg = "[PeerMessenger] Failed to resolve name: " + destName;
            _logMessage(logMsg.c_str());
        }
        return false;
    }

    // Send to the resolved ID
    return sendMessage(destId, message, requestAck);
}

bool PeerMessenger::broadcastMessage(const std::string& message) {
    return sendMessage(0xFFFF, message, false); // Broadcast to all nodes
}

bool PeerMessenger::hasMessage() const {
    return !_userMessageQueue.empty();
}

UserMessage PeerMessenger::receiveMessage() {
    if (_userMessageQueue.empty()) {
        return UserMessage{0, "", ""};
    }
    
    UserMessage_Internal userMsg = _userMessageQueue.front();
    _userMessageQueue.pop();
    
    UserMessage msg;
    msg.srcId = userMsg.srcId;
    msg.content = userMsg.content;
    
    // Try to resolve the source ID to a name
    auto& idToName = _rollCall->getIdToNameMap();
    auto it = idToName.find(userMsg.srcId);
    if (it != idToName.end()) {
        msg.srcName = it->second;
    } else {
        msg.srcName = "";
    }
    
    if (_logMessage) {
        std::string srcInfo = msg.srcName.empty() ? 
            "ID " + std::to_string(msg.srcId) : 
            msg.srcName + " (ID " + std::to_string(msg.srcId) + ")";
        std::string logMsg = "[PeerMessenger] Received from " + srcInfo + ": " + msg.content;
        _logMessage(logMsg.c_str());
    }
    
    return msg;
}

size_t PeerMessenger::getMessageCount() const {
    return _userMessageQueue.size();
}

void PeerMessenger::consoleLog(const char* message) {
    printf("%s\n", message);
}

bool PeerMessenger::sendUserMessage(uint16_t destId, const std::string& message, bool requestAck) {
    std::string fullMessage = std::string(MESSAGE_PREFIX) + message;
    
    // Send through the link layer
    return _rollCall->getLink().sendPacket(_rollCall->getNodeId(), destId, 
                                          reinterpret_cast<const uint8_t*>(fullMessage.c_str()), 
                                          fullMessage.length(), requestAck);
}