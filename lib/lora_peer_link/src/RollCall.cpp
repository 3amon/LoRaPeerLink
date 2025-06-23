#include "RollCall.h"
#include <cstring>
#include <sstream>
#include <algorithm>

RollCall::RollCall(LoRaBasicLink* link, const std::string& nodeName, 
                   time_ms_fn getTime, sleep_ms_fn sleep, random_fn getRandom)
    : _link(link), _nodeName(nodeName), _nodeId(0), 
      _getTime(getTime), _sleep(sleep), _getRandom(getRandom),
      _defaultRng(std::random_device{}()) {
    
    if (!_getRandom) {
        // Use static function pointer instead of lambda
        _getRandom = &RollCall::staticDefaultRandom;
    }
}

bool RollCall::begin() {
    // Generate initial random ID
    _nodeId = generateRandomId();
    
    // Add ourselves to the mapping
    updateMapping(_nodeName, _nodeId);
    
    // Broadcast our introduction
    if (!broadcastHelloIam()) {
        return false;
    }
    
    // Listen for potential collisions for a short period
    uint32_t start = _getTime();
    while (_getTime() - start < COLLISION_BACKOFF_MS) {
        processMessages(50);
        _sleep(50); // Add small delay to prevent busy-waiting
    }
    
    return true;
}

bool RollCall::processMessages(uint32_t timeoutMs) {
    uint8_t srcId8;
    uint8_t buffer[MAX_PAYLOAD]; // Use the constant from LoRaBasicLink
    
    // Receive packet from link layer
    int len = _link->receivePacket(&srcId8, buffer, MAX_PAYLOAD);
    if (len <= 0) {
        return false;
    }
    
    // Convert 8-bit source ID to 16-bit (link layer uses 8-bit addressing)
    uint16_t srcId = srcId8;
    
    // Convert buffer to string
    std::string message(reinterpret_cast<const char*>(buffer), len);
    
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
    if (!_link->sendPacket(BROADCAST_ADDR, 
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
    if (!_link->sendPacket(BROADCAST_ADDR, 
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
    return _link->sendPacket(BROADCAST_ADDR, 
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
    
    // Check for collision with our own ID
    if (announcedId == _nodeId && name != _nodeName) {
        return handleCollision(name, announcedId);
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
    
    // Use the destId directly if it's broadcast, otherwise truncate to 8-bit
    uint8_t dest8 = (destId == BROADCAST_ADDR) ? BROADCAST_ADDR : static_cast<uint8_t>(destId & 0xFF);
    
    return _link->sendPacket(dest8, 
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
    
    // Update mapping
    _idToName.erase(nodeId); // Remove old mapping
    updateMapping(_nodeName, newId);
    
    // Wait a random backoff time
    uint32_t backoff = 100 + (_getRandom() % 500);
    _sleep(backoff);
    
    // Broadcast new introduction
    return broadcastHelloIam();
}

std::string RollCall::parseMessage(const std::string& message, const char* prefix) {
    size_t prefixLen = strlen(prefix);
    if (message.length() >= prefixLen && message.substr(0, prefixLen) == prefix) {
        return message.substr(prefixLen);
    }
    return "";
}

uint16_t RollCall::defaultRandom() {
    return static_cast<uint16_t>(_defaultRng());
}

// Static members
std::mt19937 RollCall::_staticRng(std::random_device{}());

uint16_t RollCall::staticDefaultRandom() {
    uint16_t result = static_cast<uint16_t>(_staticRng());
    // Avoid reserved values
    while (result == 0 || result == 0xFFFF) {
        result = static_cast<uint16_t>(_staticRng());
    }
    return result;
}