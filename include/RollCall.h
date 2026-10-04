/**
 * @file RollCall.h
 * @brief RollCall protocol: node naming, ID assignment and discovery
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * RollCall lets nodes find each other by name. Every node picks a random
 * 16-bit ID, announces "I am NAME at ID" and keeps a table of the names and
 * IDs it has heard. Collisions (two nodes with the same ID or the same name)
 * are detected from the announcements and resolved.
 *
 * Messages (plain text, carried as link payloads):
 *
 *   HELLOIAM|<name> AT <id> #<nonce>   periodic announcement (broadcast)
 *   WHOIS|<name>                       who has this name?   (broadcast)
 *   WHEREIS|<id>                       who has this ID?     (broadcast)
 *   RESP|<name> AT <id>                answer to WHOIS / WHEREIS (broadcast)
 *
 * <id> and <nonce> are decimal. The nonce is a random number chosen at
 * begin(); it lets two nodes that picked the same name AND the same ID tell
 * each other apart. Receivers accept announcements without a nonce.
 *
 * Collision rules. Both nodes apply the same rule, so exactly one of them
 * changes:
 *   - Same ID, different names: the node whose name sorts later picks a new ID.
 *   - Same name, different IDs: the node with the larger ID appends a random
 *     suffix to its name ("name-1234").
 *   - Same name and same ID: the node with the larger nonce changes both.
 * The node that keeps its identity re-announces soon afterwards so that the
 * other one notices the collision even if it has not heard an announcement
 * yet.
 *
 * Malformed or oversized messages from the radio are ignored. They can never
 * crash the node.
 */

#ifndef ROLLCALL_H
#define ROLLCALL_H

#include "ILoRaLink.h"

#include <stddef.h>
#include <stdint.h>
#include <random>
#include <string>
#include <unordered_map>

/** Longest node name RollCall accepts, in bytes. */
#ifndef ROLLCALL_MAX_NAME_LEN
#define ROLLCALL_MAX_NAME_LEN 48
#endif

/** Largest number of nodes kept in the name/ID tables (this node included). */
#ifndef ROLLCALL_MAX_PEERS
#define ROLLCALL_MAX_PEERS 64
#endif

class RollCall {
public:
    using time_ms_fn = uint32_t (*)();
    using sleep_ms_fn = void (*)(uint32_t);

    /**
     * Source of random 16-bit numbers for IDs, name suffixes and timing
     * jitter. On real hardware pass a function backed by a hardware random
     * number generator (for example esp_random() on ESP32): nodes running
     * identical firmware must not produce identical sequences.
     */
    using random_fn = uint16_t (*)();

    using log_fn = void (*)(const char*);

    /**
     * Called for every received payload that is not a RollCall message, so
     * that an application layer sharing the link (for example PeerMessenger)
     * does not lose data while RollCall is listening.
     */
    using data_fn = void (*)(void* context, uint16_t srcId, const uint8_t* data, size_t len);

    /**
     * @param link       Link layer to use (must outlive this object)
     * @param nodeName   Requested name, 1..ROLLCALL_MAX_NAME_LEN bytes, without '|'
     * @param getTime    Millisecond clock
     * @param sleep      Blocking delay
     * @param getRandom  Random source, or nullptr for the built-in default
     * @param logMessage Optional log sink
     */
    RollCall(ILoRaLink* link, const std::string& nodeName,
             time_ms_fn getTime, sleep_ms_fn sleep, random_fn getRandom = nullptr,
             log_fn logMessage = nullptr);

    /**
     * @brief Pick an ID, announce this node and listen for collisions
     * @return false if the name is invalid or the announcement could not be sent
     *
     * Blocks for about COLLISION_LISTEN_MS while listening for other nodes.
     */
    bool begin();

    /**
     * @brief Receive and handle one message, and send announcements that are due
     * @param timeoutMs How long to listen for a message
     * @return true if a RollCall message was handled
     *
     * Call this regularly. The radio only needs to listen while this (or
     * another receiving call) runs, so prefer long timeouts over calling it
     * with a short timeout and sleeping in between.
     */
    bool processMessages(uint32_t timeoutMs = 100);

    /**
     * @brief Resolve a node name to its ID
     * @return The ID, or 0 if nobody answered within @p timeoutMs
     *
     * Answers from the local table when possible; otherwise broadcasts a
     * WHOIS query, QUERY_SENDS times spread over the timeout.
     */
    uint16_t whoIs(const std::string& name, uint32_t timeoutMs = 1000);

    /**
     * @brief Resolve a node ID to its name
     * @return The name, or an empty string if nobody answered within @p timeoutMs
     */
    std::string whereIs(uint16_t nodeId, uint32_t timeoutMs = 1000);

    uint16_t getNodeId() const { return _nodeId; }
    const std::string& getNodeName() const { return _nodeName; }

    const std::unordered_map<std::string, uint16_t>& getNameToIdMap() const { return _nameToId; }
    const std::unordered_map<uint16_t, std::string>& getIdToNameMap() const { return _idToName; }

    ILoRaLink& getLink() { return *_link; }

    /** Register the receiver for non-RollCall payloads (see data_fn). */
    void setDataHandler(data_fn handler, void* context) { _dataHandler = handler; _dataContext = context; }
    void* dataHandlerContext() const { return _dataContext; }

    /** True if @p message starts with one of the RollCall prefixes. */
    bool isRollCallMessage(const std::string& message) const;

    /**
     * @brief Handle one RollCall message
     * @return true if the message was well formed and handled
     */
    bool processRollCallMessage(const std::string& message, uint16_t srcId);

    /** True if @p name is acceptable as a node name. */
    static bool isValidName(const std::string& name);

    /**
     * @brief Parse a decimal node ID
     * @return true and the value in @p out if @p text is 1-5 digits in the range 1..65534
     */
    static bool parseNodeId(const std::string& text, uint16_t& out);

    static void consoleLog(const char* message);

    static constexpr uint32_t COLLISION_LISTEN_MS = 1000;           ///< begin() listens this long
    static constexpr uint32_t PERIODIC_ANNOUNCE_INTERVAL_MS = 30000; ///< Nominal announcement period
    static constexpr uint32_t ANNOUNCE_JITTER_MS = 3000;             ///< Period is randomised by +/- this much
    static constexpr uint32_t QUERY_SENDS = 3;                       ///< Times a WHOIS/WHEREIS is sent per call

private:
    ILoRaLink* _link;
    std::string _nodeName;
    uint16_t _nodeId;
    uint16_t _nonce;
    time_ms_fn _getTime;
    sleep_ms_fn _sleep;
    random_fn _getRandom;
    log_fn _logMessage;
    data_fn _dataHandler;
    void* _dataContext;

    bool _started;
    uint8_t _holdAnnouncements;   ///< >0 while a query is waiting for its answer
    uint32_t _nextAnnounceAt;   ///< Time of the next announcement

    std::unordered_map<std::string, uint16_t> _nameToId;
    std::unordered_map<uint16_t, std::string> _idToName;

    static constexpr const char* HELLOIAM_PREFIX = "HELLOIAM|";
    static constexpr const char* WHOIS_PREFIX = "WHOIS|";
    static constexpr const char* WHEREIS_PREFIX = "WHEREIS|";
    static constexpr const char* RESPONSE_PREFIX = "RESP|";

    struct Announcement {
        std::string name;
        uint16_t id = 0;
        bool hasNonce = false;
        uint16_t nonce = 0;
    };

    uint16_t generateRandomId();
    std::string generateSuffixedName(const std::string& base);

    bool send(uint16_t destId, const std::string& message);
    bool broadcastHelloIam();
    void announceIfDue();
    void scheduleAnnouncement(uint32_t minDelayMs, uint32_t spanMs);
    void expediteAnnouncement();
    void listenFor(uint32_t ms);
    void queryAndWait(const std::string& query, uint32_t timeoutMs, const std::string* name, uint16_t nodeId);

    bool handleHelloIam(const std::string& message, uint16_t srcId);
    bool handleWhois(const std::string& message, uint16_t srcId);
    bool handleWhereis(const std::string& message, uint16_t srcId);
    bool handleResponse(const std::string& message, uint16_t srcId);

    void changeId();
    void changeName();
    void learn(const std::string& name, uint16_t nodeId);
    void registerSelf();

    static bool parseAnnouncement(const std::string& content, Announcement& out);
    static bool startsWith(const std::string& message, const char* prefix);
    void log(const std::string& text);

    static uint16_t staticDefaultRandom();
    static uint32_t createSeedValue();
    static std::mt19937 _staticRng;
};

#endif // ROLLCALL_H
