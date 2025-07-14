/**
 * @file RollCall.h
 * @brief Node naming and discovery layer for LoRa peer-to-peer networks
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements a decentralized node discovery and naming system
 * that operates over any ILoRaLink implementation. It provides:
 * - Human-readable node names mapped to short numeric IDs
 * - Decentralized name resolution without central authority
 * - Collision detection and automatic ID reassignment
 * - Periodic announcements for network maintenance
 * 
 * The protocol supports several message types:
 * - HELLOIAM: Node introduction broadcasts
 * - WHOIS: Name-to-ID resolution queries
 * - WHEREIS: ID-to-name resolution queries
 * - RESP: Response messages for queries
 */

#ifndef ROLLCALL_H
#define ROLLCALL_H

#include "ILoRaLink.h"

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <queue>
#include <random>

/**
 * @class RollCall
 * @brief Node naming and discovery layer for LoRa peer-to-peer networks
 * 
 * This class implements a decentralized node discovery system that maps
 * human-readable node names (e.g., "sensor-1", "gateway-main") to short
 * 2-byte node IDs for efficient over-the-air transmission. It provides:
 * 
 * **Key Features:**
 * - **Decentralized Operation**: No central naming authority required
 * - **Collision Detection**: Automatic handling of ID conflicts
 * - **Dynamic Discovery**: Real-time node discovery and mapping
 * - **Periodic Maintenance**: Automatic network announcements
 * - **Efficient Protocol**: Minimal overhead for name resolution
 * 
 * **Protocol Messages:**
 * - `HELLOIAM <name> AT <id>`: Broadcast node introduction
 * - `WHOIS <name>`: Query for node ID by name
 * - `WHEREIS <id>`: Query for node name by ID
 * - `RESP <data>`: Response to queries
 * 
 * **Collision Handling:**
 * When multiple nodes choose the same ID, the protocol detects conflicts
 * and automatically reassigns IDs using exponential backoff to prevent
 * synchronized retries.
 * 
 * **Usage Pattern:**
 * 1. Initialize with link layer and node name
 * 2. Call begin() to generate ID and announce presence
 * 3. Regularly call processMessages() to handle incoming requests
 * 4. Use whoIs()/whereIs() for name resolution as needed
 * 5. Optional: Enable message logging for debugging protocol traffic
 * 
 * @par Example:
 * @code
 * LoRaBasicLink link(&radio, 0, getTime, sleep);
 * RollCall rollCall(&link, "sensor-1", getTime, sleep);
 * 
 * rollCall.begin();
 * 
 * // In main loop:
 * rollCall.processMessages();
 * 
 * // Look up another node:
 * uint16_t gatewayId = rollCall.whoIs("gateway-main");
 * @endcode
 * 
 * @par Example with Debug Logging:
 * @code
 * // Enable logging to see all protocol messages
 * RollCall rollCall(&link, "sensor-1", getTime, sleep, nullptr, RollCall::consoleLog);
 * @endcode
 */
class RollCall {
public:
    using time_ms_fn = uint32_t (*)();
    using sleep_ms_fn = void (*)(uint32_t);
    using random_fn = uint16_t (*)();
    using log_fn = void (*)(const char*);

    /**
     * Constructor
     * @param link Pointer to ILoRaLink for communication
     * @param nodeName Human-readable name for this node
     * @param getTime Function to get current time in milliseconds
     * @param sleep Function to sleep for specified milliseconds
     * @param getRandom Function to generate random uint16_t values (optional)
     * @param logMessage Function to log debug messages (optional, for debugging only)
     */
    RollCall(ILoRaLink* link, const std::string& nodeName, 
             time_ms_fn getTime, sleep_ms_fn sleep, random_fn getRandom = nullptr,
             log_fn logMessage = nullptr);

    /**
     * Initialize the RollCall layer
     * - Generates random node ID
     * - Broadcasts HELLOIAM message
     * - Listens for collisions
     * @return true if initialization successful
     */
    bool begin();

    /**
     * Process incoming messages and handle discovery requests
     * Should be called regularly to handle incoming packets
     * @param timeoutMs Maximum time to wait for a packet
     * @return true if a message was processed
     */
    bool processMessages(uint32_t timeoutMs = 100);

    /**
     * Look up node ID by name
     * @param name Node name to look up
     * @param timeoutMs Maximum time to wait for response
     * @return Node ID if found, 0 if not found or timeout
     */
    uint16_t whoIs(const std::string& name, uint32_t timeoutMs = 1000);

    /**
     * Look up node name by ID
     * @param nodeId Node ID to look up
     * @param timeoutMs Maximum time to wait for response
     * @return Node name if found, empty string if not found or timeout
     */
    std::string whereIs(uint16_t nodeId, uint32_t timeoutMs = 1000);

    /**
     * Get current node ID
     * @return Current node ID
     */
    uint16_t getNodeId() const { return _nodeId; }

    /**
     * Get current node name
     * @return Current node name
     */
    const std::string& getNodeName() const { return _nodeName; }

    /**
     * Get all known name-to-ID mappings
     * @return Reference to internal mapping table
     */
    const std::unordered_map<std::string, uint16_t>& getNameToIdMap() const { return _nameToId; }

    /**
     * Get all known ID-to-name mappings
     * @return Reference to internal mapping table
     */
    const std::unordered_map<uint16_t, std::string>& getIdToNameMap() const { return _idToName; }

    /**
     * Get access to the underlying link layer
     * @return Reference to ILoRaLink instance
     */
    ILoRaLink& getLink() { return *_link; }

    /**
     * Check if a message is a RollCall protocol message
     * @param message Message content to check
     * @return true if the message is a RollCall protocol message
     */
    bool isRollCallMessage(const std::string& message) const;

    /**
     * Process a RollCall protocol message
     * @param message Message content
     * @param srcId Source node ID from transport layer
     * @return true if message was processed successfully
     */
    bool processRollCallMessage(const std::string& message, uint16_t srcId);

    /**
     * Default console logging function for debugging
     * @param message Message to log to console
     */
    static void consoleLog(const char* message);

private:
    ILoRaLink* _link;
    std::string _nodeName;
    uint16_t _nodeId;
    time_ms_fn _getTime;
    sleep_ms_fn _sleep;
    random_fn _getRandom;
    log_fn _logMessage;

    // Timing for periodic announcements
    uint32_t _lastAnnouncementTime;

    // Mapping tables
    std::unordered_map<std::string, uint16_t> _nameToId;
    std::unordered_map<uint16_t, std::string> _idToName;

    // Protocol constants
    static constexpr const char* HELLOIAM_PREFIX = "HELLOIAM|";
    static constexpr const char* WHOIS_PREFIX = "WHOIS|";
    static constexpr const char* WHEREIS_PREFIX = "WHEREIS|";
    static constexpr const char* RESPONSE_PREFIX = "RESP|";
    static constexpr uint32_t COLLISION_BACKOFF_MS = 1000;
    static constexpr uint32_t DISCOVERY_TIMEOUT_MS = 1000;
    static constexpr uint32_t PERIODIC_ANNOUNCE_INTERVAL_MS = 30000; // 30 seconds
    static constexpr int MAX_RETRIES = 3;

    /**
     * Generate a random 2-byte node ID
     * @return Random node ID (avoiding 0 and 0xFFFF)
     */
    uint16_t generateRandomId();

    /**
     * Broadcast HELLOIAM message
     * @return true if message sent successfully
     */
    bool broadcastHelloIam();

    /**
     * Handle incoming HELLOIAM message
     * @param message Full message content
     * @param srcId Source node ID
     * @return true if message processed successfully
     */
    bool handleHelloIam(const std::string& message, uint16_t srcId);

    /**
     * Handle incoming WHOIS message
     * @param message Full message content
     * @param srcId Source node ID
     * @return true if message processed successfully
     */
    bool handleWhois(const std::string& message, uint16_t srcId);

    /**
     * Handle incoming WHEREIS message
     * @param message Full message content
     * @param srcId Source node ID
     * @return true if message processed successfully
     */
    bool handleWhereis(const std::string& message, uint16_t srcId);

    /**
     * Handle incoming RESP message
     * @param message Full message content
     * @param srcId Source node ID
     * @return true if message processed successfully
     */
    bool handleResponse(const std::string& message, uint16_t srcId);

    /**
     * Send response message
     * @param destId Destination node ID
     * @param response Response content
     * @return true if message sent successfully
     */
    bool sendResponse(uint16_t destId, const std::string& response);

    /**
     * Update mapping tables with new name-ID pair
     * @param name Node name
     * @param nodeId Node ID
     */
    void updateMapping(const std::string& name, uint16_t nodeId);

    /**
     * Check for ID collision and reassign if necessary
     * @param name Node name that might be conflicting
     * @param nodeId Node ID that might be conflicting
     * @return true if collision detected and handled
     */
    bool handleCollision(const std::string& name, uint16_t nodeId);

    /**
     * Check for name collision and reassign name if necessary
     * @param conflictingName Node name that is conflicting
     * @param conflictingNodeId Node ID of the conflicting node
     * @return true if collision detected and handled
     */
    bool handleNameCollision(const std::string& conflictingName, uint16_t conflictingNodeId);

    /**
     * Handle complete collision where both name and ID match but from different transport source
     * @param conflictingName Node name that is conflicting (same as ours)
     * @param conflictingNodeId Node ID that is conflicting (same as ours)
     * @return true if collision detected and handled
     */
    bool handleCompleteCollision(const std::string& conflictingName, uint16_t conflictingNodeId);

    /**
     * Parse message content after prefix
     * @param message Full message
     * @param prefix Expected prefix
     * @return Content after prefix, or empty string if prefix doesn't match
     */
    std::string parseMessage(const std::string& message, const char* prefix);

    // Static random number generator for default case
    static uint32_t createSeedValue(time_ms_fn getTime);
    
    // Static wrapper for default random function
    static uint16_t staticDefaultRandom();
    static std::mt19937 _staticRng;
};

#endif // ROLLCALL_H