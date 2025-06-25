/**
 * @file RollCall.cpp
 * @brief Implementation of decentralized node naming and discovery system
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements the RollCall class, providing a decentralized node
 * discovery and naming system for LoRa peer-to-peer networks. It handles
 * node registration, name resolution, collision detection, and maintains
 * distributed naming tables without requiring a central authority.
 */

#include "RollCall.h"
#include "LoraBasicLink.h"  // For MAX_PAYLOAD constant
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cstdio>  // For printf

/**
 * @brief Constructor initializes RollCall with link layer and node configuration
 * 
 * Sets up the node discovery system with the provided link layer interface
 * and node identification. Initializes random number generation and prepares
 * the system for decentralized name resolution operations.
 */
RollCall::RollCall(ILoRaLink* link, const std::string& nodeName, 
                   time_ms_fn getTime, sleep_ms_fn sleep, random_fn getRandom,
                   log_fn logMessage)
    : _link(link), _nodeName(nodeName), _nodeId(0), 
      _getTime(getTime), _sleep(sleep), _getRandom(getRandom),
      _logMessage(logMessage), _lastAnnouncementTime(0), _defaultRng(createSeedValue(getTime)) {
    
    // Set up random number generation if not provided
    if (!_getRandom) {
        // Use static function pointer for default random generation but with improved seeding
        _getRandom = &RollCall::staticDefaultRandom;
    }
}

/**
 * @brief Initialize the RollCall system and announce node presence
 * 
 * Performs initial setup for the node discovery system:
 * 1. Generates a random unique node ID
 * 2. Registers this node in the local mapping tables
 * 3. Broadcasts initial HELLOIAM announcement to the network
 * 
 * This method must be called before using other RollCall functionality.
 */
bool RollCall::begin() {
    // Generate initial random ID for this node
    _nodeId = generateRandomId();
    
    // Set the local ID on the link layer for address filtering
    _link->setLocalId(_nodeId);
    
    // Register ourselves in the local mapping tables
    updateMapping(_nodeName, _nodeId);
    
    // Announce our presence to the network
    if (!broadcastHelloIam()) {
        return false;
    }
    
    // Record the time of our initial announcement for periodic rebroadcasting
    _lastAnnouncementTime = _getTime();
    
    // Listen for potential collisions for a short period
    // But don't be too aggressive about consuming messages during startup
    uint32_t start = _getTime();
    uint32_t lastCheck = start;
    while (_getTime() - start < COLLISION_BACKOFF_MS) {
        uint32_t now = _getTime();
        if (now - lastCheck >= 100) { // Only check every 100ms
            processMessages(10); // Short timeout to avoid consuming too many messages
            lastCheck = now;
        }
        _sleep(50); // Sleep most of the time
    }
    
    return true;
}

bool RollCall::processMessages(uint32_t timeoutMs) {
    // Check if it's time for periodic announcement
    uint32_t currentTime = _getTime();
    if (currentTime - _lastAnnouncementTime >= PERIODIC_ANNOUNCE_INTERVAL_MS) {
        broadcastHelloIam();
        _lastAnnouncementTime = currentTime;
    }
    
    uint16_t srcId;
    uint8_t buffer[MAX_PAYLOAD]; // Use the constant from LoRaBasicLink
    
    // Receive packet from link layer
    int len = _link->receivePacket(&srcId, buffer, MAX_PAYLOAD);
    if (len <= 0) {
        return false;
    }
    
    // Convert buffer to string
    std::string message(reinterpret_cast<const char*>(buffer), len);
    
    // Log the received message if logging is enabled
    if (_logMessage) {
        std::string logMsg = "[RollCall] Received: " + message + " from ID " + std::to_string(srcId);
        _logMessage(logMsg.c_str());
    }
    
    // Handle different message types
    if (message.find(HELLOIAM_PREFIX) == 0) {
        return handleHelloIam(message, srcId);
    } else if (message.find(WHOIS_PREFIX) == 0) {
        return handleWhois(message, srcId);
    } else if (message.find(WHEREIS_PREFIX) == 0) {
        return handleWhereis(message, srcId);
    } else if (message.find(RESPONSE_PREFIX) == 0) {
        return handleResponse(message, srcId);
    }
    
    return false;
}

uint16_t RollCall::whoIs(const std::string& name, uint32_t timeoutMs) {
    // Check local cache first
    auto it = _nameToId.find(name);
    if (it != _nameToId.end()) {
        return it->second;
    }
    
    // Broadcast WHOIS query
    std::string query = std::string(WHOIS_PREFIX) + name;
    
    // Log the outgoing query if logging is enabled
    if (_logMessage) {
        std::string logMsg = "[RollCall] Sending: " + query;
        _logMessage(logMsg.c_str());
    }
    
    if (!_link->sendPacket(_nodeId, BROADCAST_ADDR, 
                          reinterpret_cast<const uint8_t*>(query.c_str()), 
                          query.length())) {
        return 0;
    }
    
    // Wait for response
    uint32_t start = _getTime();
    while (_getTime() - start < timeoutMs) {
        processMessages(50); // This will return quickly if no messages
        
        // Check if we got the answer
        auto it = _nameToId.find(name);
        if (it != _nameToId.end()) {
            return it->second;
        }
        
        // Small delay to prevent busy-waiting
        _sleep(10);
    }
    
    return 0; // Not found
}

std::string RollCall::whereIs(uint16_t nodeId, uint32_t timeoutMs) {
    // Check local cache first
    auto it = _idToName.find(nodeId);
    if (it != _idToName.end()) {
        return it->second;
    }
    
    // Broadcast WHEREIS query
    std::string query = std::string(WHEREIS_PREFIX) + std::to_string(nodeId);
    
    // Log the outgoing query if logging is enabled
    if (_logMessage) {
        std::string logMsg = "[RollCall] Sending: " + query;
        _logMessage(logMsg.c_str());
    }
    
    if (!_link->sendPacket(_nodeId, BROADCAST_ADDR, 
                          reinterpret_cast<const uint8_t*>(query.c_str()), 
                          query.length())) {
        return "";
    }
    
    // Wait for response
    uint32_t start = _getTime();
    while (_getTime() - start < timeoutMs) {
        processMessages(50); // This will return quickly if no messages
        
        // Check if we got the answer
        auto it = _idToName.find(nodeId);
        if (it != _idToName.end()) {
            return it->second;
        }
        
        // Small delay to prevent busy-waiting
        _sleep(10);
    }
    
    return ""; // Not found
}

uint16_t RollCall::generateRandomId() {
    uint16_t id;
    do {
        id = _getRandom();
    } while (id == 0 || id == 0xFFFF); // Avoid reserved values
    return id;
}

bool RollCall::broadcastHelloIam() {
    std::string message = std::string(HELLOIAM_PREFIX) + _nodeName + " AT " + std::to_string(_nodeId);
    
    // Log the outgoing message if logging is enabled
    if (_logMessage) {
        std::string logMsg = "[RollCall] Sending: " + message;
        _logMessage(logMsg.c_str());
    }
    
    return _link->sendPacket(_nodeId, BROADCAST_ADDR, 
                            reinterpret_cast<const uint8_t*>(message.c_str()), 
                            message.length());
}

bool RollCall::handleHelloIam(const std::string& message, uint16_t srcId) {
    std::string content = parseMessage(message, HELLOIAM_PREFIX);
    if (content.empty()) {
        return false;
    }
    
    // Parse "NAME AT ID" format
    size_t atPos = content.find(" AT ");
    if (atPos == std::string::npos) {
        return false;
    }
    
    std::string name = content.substr(0, atPos);
    std::string idStr = content.substr(atPos + 4);
    uint16_t announcedId = static_cast<uint16_t>(std::stoul(idStr));
    
    // Check for complete collision: same name AND same ID from different transport source
    // This indicates another node has randomly chosen our exact identity
    if (announcedId == _nodeId && name == _nodeName && srcId != _nodeId) {
        // This is a complete collision - both name and ID match but it's from a different node
        // We need to change both our name and ID
        return handleCompleteCollision(name, announcedId);
    }
    
    // Check for collision with our own ID
    if (announcedId == _nodeId && name != _nodeName) {
        return handleCollision(name, announcedId);
    }
    
    // Check for name collision with our own name
    if (name == _nodeName && announcedId != _nodeId) {
        return handleNameCollision(name, announcedId);
    }
    
    // Update mapping - use the announced ID, not the transport layer source ID
    updateMapping(name, announcedId);
    return true;
}

bool RollCall::handleWhois(const std::string& message, uint16_t srcId) {
    std::string name = parseMessage(message, WHOIS_PREFIX);
    if (name.empty()) {
        return false;
    }
    
    // Check if we know this name
    auto it = _nameToId.find(name);
    if (it != _nameToId.end()) {
        std::string response = name + " AT " + std::to_string(it->second);
        // Send response as broadcast since we can't map RollCall IDs back to transport IDs
        return sendResponse(BROADCAST_ADDR, response);
    }
    
    return false;
}

bool RollCall::handleWhereis(const std::string& message, uint16_t srcId) {
    std::string idStr = parseMessage(message, WHEREIS_PREFIX);
    if (idStr.empty()) {
        return false;
    }
    
    uint16_t nodeId = static_cast<uint16_t>(std::stoul(idStr));
    
    // Check if we know this ID
    auto it = _idToName.find(nodeId);
    if (it != _idToName.end()) {
        std::string response = it->second + " AT " + std::to_string(nodeId);
        // Send response as broadcast since we can't map RollCall IDs back to transport IDs
        return sendResponse(BROADCAST_ADDR, response);
    }
    
    return false;
}

bool RollCall::handleResponse(const std::string& message, uint16_t srcId) {
    std::string content = parseMessage(message, RESPONSE_PREFIX);
    if (content.empty()) {
        return false;
    }
    
    // Parse "NAME AT ID" format
    size_t atPos = content.find(" AT ");
    if (atPos == std::string::npos) {
        return false;
    }
    
    std::string name = content.substr(0, atPos);
    std::string idStr = content.substr(atPos + 4);
    uint16_t nodeId = static_cast<uint16_t>(std::stoul(idStr));
    
    // Update mapping
    updateMapping(name, nodeId);
    return true;
}

bool RollCall::sendResponse(uint16_t destId, const std::string& response) {
    std::string message = std::string(RESPONSE_PREFIX) + response;
    
    // Log the outgoing response if logging is enabled
    if (_logMessage) {
        std::string logMsg = "[RollCall] Sending: " + message + " to ID " + std::to_string(destId);
        _logMessage(logMsg.c_str());
    }
    
    return _link->sendPacket(_nodeId, destId, 
                            reinterpret_cast<const uint8_t*>(message.c_str()), 
                            message.length());
}

void RollCall::updateMapping(const std::string& name, uint16_t nodeId) {
    _nameToId[name] = nodeId;
    _idToName[nodeId] = name;
}

bool RollCall::handleCollision(const std::string& name, uint16_t nodeId) {
    // Generate new random ID
    uint16_t newId = generateRandomId();
    
    // Update our own ID
    _nodeId = newId;
    
    // Update the link layer with our new local ID for address filtering
    _link->setLocalId(_nodeId);
    
    // Update mapping
    _idToName.erase(nodeId); // Remove old mapping
    updateMapping(_nodeName, newId);
    
    // Wait a random backoff time
    uint32_t backoff = 100 + (_getRandom() % 500);
    _sleep(backoff);
    
    // Broadcast new introduction
    return broadcastHelloIam();
}

bool RollCall::handleNameCollision(const std::string& conflictingName, uint16_t conflictingNodeId) {
    // Generate unique suffix for our name (not using nodeID for privacy)
    uint16_t randomSuffix = _getRandom() % 10000; // 4-digit random number
    std::string newName = _nodeName + "-" + std::to_string(randomSuffix);
    
    // Ensure the new name is unique by checking if we already know about it
    while (_nameToId.find(newName) != _nameToId.end()) {
        randomSuffix = _getRandom() % 10000;
        newName = _nodeName + "-" + std::to_string(randomSuffix);
    }
    
    // Update our name and mapping
    std::string oldName = _nodeName;
    _nodeName = newName;
    
    // Remove old mapping and add new one
    _nameToId.erase(oldName);
    updateMapping(_nodeName, _nodeId);
    
    // Also update the mapping for the conflicting node
    updateMapping(conflictingName, conflictingNodeId);
    
    // Wait a random backoff time
    uint32_t backoff = 100 + (_getRandom() % 500);
    _sleep(backoff);
    
    // Broadcast new introduction with new name
    return broadcastHelloIam();
}

bool RollCall::handleCompleteCollision(const std::string& conflictingName, uint16_t conflictingNodeId) {
    // This is a complete collision - another node has chosen both our name and our ID
    // We need to change both our name and our ID
    
    // Generate new random ID
    uint16_t newId = generateRandomId();
    
    // Generate unique suffix for our name
    uint16_t randomSuffix = _getRandom() % 10000; // 4-digit random number
    std::string newName = _nodeName + "-" + std::to_string(randomSuffix);
    
    // Ensure the new name is unique by checking if we already know about it
    while (_nameToId.find(newName) != _nameToId.end()) {
        randomSuffix = _getRandom() % 10000;
        newName = _nodeName + "-" + std::to_string(randomSuffix);
    }
    
    // Store old values for cleanup
    std::string oldName = _nodeName;
    uint16_t oldId = _nodeId;
    
    // Update our identity
    _nodeName = newName;
    _nodeId = newId;
    
    // Update the link layer with our new local ID for address filtering
    _link->setLocalId(_nodeId);
    
    // Clean up old mappings
    _nameToId.erase(oldName);
    _idToName.erase(oldId);
    
    // Add new mapping for ourselves
    updateMapping(_nodeName, _nodeId);
    
    // Also update the mapping for the conflicting node
    updateMapping(conflictingName, conflictingNodeId);
    
    // Wait a random backoff time (longer for complete collision)
    uint32_t backoff = 200 + (_getRandom() % 1000);
    _sleep(backoff);
    
    // Broadcast new introduction with new identity
    return broadcastHelloIam();
}

std::string RollCall::parseMessage(const std::string& message, const char* prefix) {
    size_t prefixLen = strlen(prefix);
    if (message.length() >= prefixLen && message.substr(0, prefixLen) == prefix) {
        return message.substr(prefixLen);
    }
    return "";
}

uint32_t RollCall::createSeedValue(time_ms_fn getTime) {
    uint32_t seed = 0;
    
    // Try to get entropy from std::random_device
    try {
        std::random_device rd;
        seed = rd();
        
        // If std::random_device entropy is low (common on embedded platforms),
        // mix in additional entropy sources
        if (rd.entropy() < 2.0) {
            // Add time-based entropy if available
            if (getTime) {
                seed ^= getTime();
            }
            
            // Add some memory address entropy (varies between runs)
            seed ^= reinterpret_cast<uintptr_t>(&seed);
            
            // Simple hash mixing to improve distribution
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
        }
    } catch (...) {
        // Fallback if std::random_device fails
        if (getTime) {
            seed = getTime();
        } else {
            seed = 12345; // Last resort fallback
        }
        
        // Mix in memory addresses for uniqueness
        seed ^= reinterpret_cast<uintptr_t>(&seed);
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
    }
    
    return seed;
}

uint32_t RollCall::createSeed() {
    return createSeedValue(_getTime);
}

uint16_t RollCall::defaultRandom() {
    uint16_t result = static_cast<uint16_t>(_defaultRng());
    // Avoid reserved values
    while (result == 0 || result == 0xFFFF) {
        result = static_cast<uint16_t>(_defaultRng());
    }
    return result;
}

// Static members with improved seeding
std::mt19937 RollCall::_staticRng(RollCall::createSeedValue(nullptr));

uint16_t RollCall::staticDefaultRandom() {
    uint16_t result = static_cast<uint16_t>(_staticRng());
    // Avoid reserved values
    while (result == 0 || result == 0xFFFF) {
        result = static_cast<uint16_t>(_staticRng());
    }
    return result;
}

void RollCall::consoleLog(const char* message) {
    // Simple console output for debugging
    printf("%s\n", message);
}