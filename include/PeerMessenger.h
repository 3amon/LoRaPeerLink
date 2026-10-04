/**
 * @file PeerMessenger.h
 * @brief High-level text messaging between named LoRa nodes
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * PeerMessenger is the top of the stack: send a message to a node by name or
 * ID, or to everyone, and read received messages from a queue. It uses
 * RollCall for name resolution and shares RollCall's link.
 *
 * Messages travel as "MSG|<text>" payloads. PeerMessenger registers itself
 * as RollCall's data handler, so a message is queued no matter which of the
 * two objects happens to be listening when it arrives, including while a
 * name is being resolved.
 */

#ifndef PEER_MESSENGER_H
#define PEER_MESSENGER_H

#include "RollCall.h"

#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <string>

/** Largest number of received messages kept until the application reads them. */
#ifndef PEER_MESSENGER_MAX_QUEUE
#define PEER_MESSENGER_MAX_QUEUE 16
#endif

/**
 * @struct UserMessage
 * @brief A received user message
 */
struct UserMessage {
    uint16_t srcId;           ///< Source node ID
    std::string srcName;      ///< Source node name ("" if unknown)
    std::string content;      ///< Message text (may contain any bytes)
};

class PeerMessenger {
public:
    using log_fn = void (*)(const char*);

    /**
     * @param rollCall   RollCall instance to use (must outlive this object)
     * @param logMessage Optional log sink
     */
    PeerMessenger(RollCall* rollCall, log_fn logMessage = nullptr);
    ~PeerMessenger();

    PeerMessenger(const PeerMessenger&) = delete;
    PeerMessenger& operator=(const PeerMessenger&) = delete;

    /** @return false if there is no RollCall instance. RollCall::begin() must be called separately. */
    bool begin();

    /**
     * @brief Listen for one message and keep RollCall running
     * @param timeoutMs How long to listen
     * @return true if a user message was queued or a RollCall message was handled
     *
     * This is the only call an application needs in its main loop; there is
     * no need to call RollCall::processMessages() as well.
     */
    bool processMessages(uint32_t timeoutMs = 100);

    /**
     * @brief Send a message to a node ID
     * @return false if the message is too long (see maxMessageLength()), the
     *         radio failed, or an acknowledgment was requested and none arrived
     */
    bool sendMessage(uint16_t destId, const std::string& message, bool requestAck = false);

    /**
     * @brief Send a message to a node by name
     * @param timeoutMs How long to wait for the name to be resolved
     * @return false if the name could not be resolved or sending failed
     */
    bool sendMessage(const std::string& destName, const std::string& message,
                     bool requestAck = false, uint32_t timeoutMs = 1000);

    /** Send a message to every node in range (never acknowledged). */
    bool broadcastMessage(const std::string& message);

    bool hasMessage() const;

    /** Remove and return the oldest received message (srcId 0 if the queue is empty). */
    UserMessage receiveMessage();

    size_t getMessageCount() const;

    /** Longest message text that fits in one packet on the current link. */
    size_t maxMessageLength() const;

    /** Messages discarded because the queue was full (oldest are dropped first). */
    uint32_t droppedMessages() const { return _dropped; }

    RollCall& getRollCall() { return *_rollCall; }

    static void consoleLog(const char* message);

private:
    struct QueuedMessage {
        uint16_t srcId;
        std::string content;
    };

    static void onData(void* context, uint16_t srcId, const uint8_t* data, size_t len);
    void handleData(uint16_t srcId, const uint8_t* data, size_t len);
    void log(const std::string& text);

    RollCall* _rollCall;
    log_fn _logMessage;
    std::deque<QueuedMessage> _queue;
    uint32_t _received;   ///< Total number of user messages queued so far
    uint32_t _dropped;

    static constexpr const char* MESSAGE_PREFIX = "MSG|";
    static constexpr size_t MESSAGE_PREFIX_LEN = 4;
};

#endif // PEER_MESSENGER_H
