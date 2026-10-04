/**
 * @file PeerMessenger.cpp
 * @brief High-level text messaging between named LoRa nodes
 * @author LoRaPeerLink Project
 * @version 2.0
 */

#include "PeerMessenger.h"

#include <cstdio>
#include <cstring>

constexpr size_t PeerMessenger::MESSAGE_PREFIX_LEN;

PeerMessenger::PeerMessenger(RollCall* rollCall, log_fn logMessage)
    : _rollCall(rollCall), _logMessage(logMessage), _received(0), _dropped(0) {
    if (!_rollCall) {
        log("[PeerMessenger] Error: RollCall instance is null");
        return;
    }
    // Receive every non-RollCall payload, whoever is listening at the time.
    _rollCall->setDataHandler(&PeerMessenger::onData, this);
}

PeerMessenger::~PeerMessenger() {
    if (_rollCall && _rollCall->dataHandlerContext() == this) {
        _rollCall->setDataHandler(nullptr, nullptr);
    }
}

bool PeerMessenger::begin() {
    if (!_rollCall) {
        return false;
    }
    log("[PeerMessenger] Initialized for node: " + _rollCall->getNodeName() +
        " (ID: " + std::to_string(_rollCall->getNodeId()) + ")");
    return true;
}

bool PeerMessenger::processMessages(uint32_t timeoutMs) {
    if (!_rollCall) {
        return false;
    }
    // RollCall does the receiving: it handles its own messages, sends the
    // periodic announcements, and passes everything else to onData().
    const uint32_t before = _received;
    const bool handledRollCall = _rollCall->processMessages(timeoutMs);
    return handledRollCall || _received != before;
}

void PeerMessenger::onData(void* context, uint16_t srcId, const uint8_t* data, size_t len) {
    static_cast<PeerMessenger*>(context)->handleData(srcId, data, len);
}

void PeerMessenger::handleData(uint16_t srcId, const uint8_t* data, size_t len) {
    if (len < MESSAGE_PREFIX_LEN || memcmp(data, MESSAGE_PREFIX, MESSAGE_PREFIX_LEN) != 0) {
        return;     // Not a user message
    }

    QueuedMessage msg;
    msg.srcId = srcId;
    msg.content.assign(reinterpret_cast<const char*>(data) + MESSAGE_PREFIX_LEN, len - MESSAGE_PREFIX_LEN);

    if (_queue.size() >= PEER_MESSENGER_MAX_QUEUE) {
        _queue.pop_front();     // Keep the newest messages
        ++_dropped;
    }
    log("[PeerMessenger] Received user message from ID " + std::to_string(srcId) + ": " + msg.content);
    _queue.push_back(std::move(msg));
    ++_received;
}

size_t PeerMessenger::maxMessageLength() const {
    if (!_rollCall) return 0;
    const size_t linkMax = _rollCall->getLink().maxPayloadSize();
    return linkMax > MESSAGE_PREFIX_LEN ? linkMax - MESSAGE_PREFIX_LEN : 0;
}

bool PeerMessenger::sendMessage(uint16_t destId, const std::string& message, bool requestAck) {
    if (!_rollCall) {
        return false;
    }
    if (message.size() > maxMessageLength()) {
        log("[PeerMessenger] Message too long (" + std::to_string(message.size()) + " bytes, limit " +
            std::to_string(maxMessageLength()) + ")");
        return false;
    }

    log("[PeerMessenger] Sending to ID " + std::to_string(destId) + ": " + message);

    const std::string payload = std::string(MESSAGE_PREFIX) + message;
    return _rollCall->getLink().sendPacket(_rollCall->getNodeId(), destId,
                                           reinterpret_cast<const uint8_t*>(payload.data()),
                                           static_cast<uint8_t>(payload.size()), requestAck);
}

bool PeerMessenger::sendMessage(const std::string& destName, const std::string& message,
                                bool requestAck, uint32_t timeoutMs) {
    if (!_rollCall) {
        return false;
    }
    if (message.size() > maxMessageLength()) {
        log("[PeerMessenger] Message too long (" + std::to_string(message.size()) + " bytes, limit " +
            std::to_string(maxMessageLength()) + ")");
        return false;
    }

    const uint16_t destId = _rollCall->whoIs(destName, timeoutMs);
    if (destId == 0) {
        log("[PeerMessenger] Failed to resolve name: " + destName);
        return false;
    }
    return sendMessage(destId, message, requestAck);
}

bool PeerMessenger::broadcastMessage(const std::string& message) {
    return sendMessage(static_cast<uint16_t>(0xFFFF), message, false);
}

bool PeerMessenger::hasMessage() const {
    return !_queue.empty();
}

UserMessage PeerMessenger::receiveMessage() {
    if (_queue.empty()) {
        return UserMessage{0, "", ""};
    }

    QueuedMessage queued = std::move(_queue.front());
    _queue.pop_front();

    UserMessage msg;
    msg.srcId = queued.srcId;
    msg.content = std::move(queued.content);

    const auto& idToName = _rollCall->getIdToNameMap();
    auto it = idToName.find(msg.srcId);
    msg.srcName = (it != idToName.end()) ? it->second : "";

    if (_logMessage) {
        const std::string srcInfo = msg.srcName.empty()
            ? "ID " + std::to_string(msg.srcId)
            : msg.srcName + " (ID " + std::to_string(msg.srcId) + ")";
        log("[PeerMessenger] Received from " + srcInfo + ": " + msg.content);
    }
    return msg;
}

size_t PeerMessenger::getMessageCount() const {
    return _queue.size();
}

void PeerMessenger::log(const std::string& text) {
    if (_logMessage) {
        _logMessage(text.c_str());
    }
}

void PeerMessenger::consoleLog(const char* message) {
    printf("%s\n", message);
}
