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

    // Let RollCall handle all messages including user messages
    return _rollCall->processMessages(timeoutMs);
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
    
    // Send through RollCall's user message capability
    return _rollCall->sendUserMessage(destId, message, requestAck);
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
    return _rollCall && _rollCall->hasUserMessage();
}

UserMessage PeerMessenger::receiveMessage() {
    if (!_rollCall || !_rollCall->hasUserMessage()) {
        return UserMessage{0, "", ""};
    }
    
    uint16_t srcId;
    std::string content;
    if (_rollCall->receiveUserMessage(&srcId, &content)) {
        UserMessage msg;
        msg.srcId = srcId;
        msg.content = content;
        
        // Try to resolve the source ID to a name
        auto& idToName = _rollCall->getIdToNameMap();
        auto it = idToName.find(srcId);
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
    
    return UserMessage{0, "", ""};
}

size_t PeerMessenger::getMessageCount() const {
    return _rollCall ? _rollCall->getUserMessageCount() : 0;
}

void PeerMessenger::consoleLog(const char* message) {
    printf("%s\n", message);
}