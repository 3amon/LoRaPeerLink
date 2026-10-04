/**
 * @file RollCall.cpp
 * @brief RollCall protocol: node naming, ID assignment and discovery
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * See RollCall.h for the message formats and the collision rules.
 */

#include "RollCall.h"

#include <cstdio>
#include <cstring>

#if defined(ESP_PLATFORM)
#if defined(__has_include)
#if __has_include(<esp_random.h>)
#include <esp_random.h>
#else
#include <esp_system.h>
#endif
#else
#include <esp_system.h>
#endif
#endif

namespace {
const uint16_t kBroadcast = 0xFFFF;
const size_t kRxBufferSize = 255;
}

constexpr uint32_t RollCall::COLLISION_LISTEN_MS;
constexpr uint32_t RollCall::PERIODIC_ANNOUNCE_INTERVAL_MS;
constexpr uint32_t RollCall::ANNOUNCE_JITTER_MS;
constexpr uint32_t RollCall::QUERY_SENDS;

RollCall::RollCall(ILoRaLink* link, const std::string& nodeName,
                   time_ms_fn getTime, sleep_ms_fn sleep, random_fn getRandom,
                   log_fn logMessage)
    : _link(link), _nodeName(nodeName), _nodeId(0), _nonce(0),
      _getTime(getTime), _sleep(sleep), _getRandom(getRandom),
      _logMessage(logMessage), _dataHandler(nullptr), _dataContext(nullptr),
      _started(false), _holdAnnouncements(0), _nextAnnounceAt(0) {
    if (!_getRandom) {
        _getRandom = &RollCall::staticDefaultRandom;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool RollCall::begin() {
    if (!_link || !isValidName(_nodeName)) {
        return false;
    }

    _nameToId.clear();
    _idToName.clear();
    _nodeId = generateRandomId();
    _nonce = _getRandom();
    _link->setLocalId(_nodeId);
    registerSelf();
    _started = true;

    // Nodes that are switched on together must not all announce at the same
    // instant, so start with a short random listening period.
    _nextAnnounceAt = _getTime() + PERIODIC_ANNOUNCE_INTERVAL_MS;
    listenFor(_getRandom() % 500);

    if (!broadcastHelloIam()) {
        return false;
    }

    // Listen for nodes that object to our name or ID. Unlike version 1 this
    // keeps the radio receiving for the whole period.
    listenFor(COLLISION_LISTEN_MS);

    // Announce once more fairly soon (the first announcement may have
    // collided with somebody else's), then settle into the periodic schedule.
    scheduleAnnouncement(1000, 3000);
    return true;
}

bool RollCall::processMessages(uint32_t timeoutMs) {
    if (!_link) return false;

    const uint32_t start = _getTime();
    for (;;) {
        announceIfDue();

        const uint32_t before = _getTime();
        const uint32_t elapsed = before - start;
        const uint32_t remaining = timeoutMs > elapsed ? timeoutMs - elapsed : 0;

        // Do not listen past the next announcement. Announcements have to go
        // out at their own (randomised) times; if they were only sent when a
        // receive window happens to end, all nodes that just heard the same
        // packet would transmit at the same instant.
        uint32_t window = remaining;
        if (_started && _holdAnnouncements == 0) {
            const int32_t untilAnnounce = static_cast<int32_t>(_nextAnnounceAt - before);
            if (untilAnnounce > 0 && static_cast<uint32_t>(untilAnnounce) < window) {
                window = static_cast<uint32_t>(untilAnnounce);
            }
        }

        uint16_t srcId = 0;
        uint8_t buffer[kRxBufferSize];
        const int len = _link->receivePacket(&srcId, buffer, static_cast<uint8_t>(kRxBufferSize), window);
        if (len > 0) {
            const std::string message(reinterpret_cast<const char*>(buffer), static_cast<size_t>(len));
            if (isRollCallMessage(message)) {
                return processRollCallMessage(message, srcId);
            }
            // Not ours: hand it to the application layer instead of dropping it.
            if (_dataHandler) {
                _dataHandler(_dataContext, srcId, buffer, static_cast<size_t>(len));
            }
            return false;
        }

        if (window >= remaining) {
            return false;       // The caller's timeout is over
        }
        if (_getTime() == before) {
            return false;       // Non-blocking radio and a frozen clock: do not spin
        }
        // Otherwise we only stopped early to send an announcement: go round again.
    }
}

uint16_t RollCall::whoIs(const std::string& name, uint32_t timeoutMs) {
    if (!isValidName(name)) return 0;

    auto cached = _nameToId.find(name);
    if (cached != _nameToId.end()) {
        return cached->second;
    }

    queryAndWait(std::string(WHOIS_PREFIX) + name, timeoutMs, &name, 0);

    auto it = _nameToId.find(name);
    return it != _nameToId.end() ? it->second : 0;
}

std::string RollCall::whereIs(uint16_t nodeId, uint32_t timeoutMs) {
    if (nodeId == 0 || nodeId == kBroadcast) return "";

    auto cached = _idToName.find(nodeId);
    if (cached != _idToName.end()) {
        return cached->second;
    }

    queryAndWait(std::string(WHEREIS_PREFIX) + std::to_string(nodeId), timeoutMs, nullptr, nodeId);

    auto it = _idToName.find(nodeId);
    return it != _idToName.end() ? it->second : "";
}

void RollCall::queryAndWait(const std::string& query, uint32_t timeoutMs, const std::string* name, uint16_t nodeId) {
    if (!_link || !send(kBroadcast, query)) {
        return;
    }

    // While waiting for the answer, do not interrupt the listening to send
    // one of our own announcements; it would be on the air exactly when the
    // answer arrives. The announcement goes out afterwards.
    ++_holdAnnouncements;

    // The query goes out QUERY_SENDS times, evenly spread over the timeout, in
    // case the query or its answer is lost.
    const uint32_t start = _getTime();
    uint32_t sends = 1;
    for (;;) {
        const uint32_t before = _getTime();
        const uint32_t elapsed = before - start;
        if (elapsed >= timeoutMs) break;

        uint32_t windowEnd = timeoutMs;
        if (sends < QUERY_SENDS) {
            const uint32_t nextSendAt = (timeoutMs / QUERY_SENDS) * sends;
            if (elapsed >= nextSendAt) {
                send(kBroadcast, query);
                ++sends;
                continue;
            }
            windowEnd = nextSendAt;
        }
        const uint32_t window = windowEnd - elapsed;

        processMessages(window);

        const bool found = name ? (_nameToId.find(*name) != _nameToId.end())
                                : (_idToName.find(nodeId) != _idToName.end());
        if (found) break;
        if (_getTime() == before) {
            _sleep(window < 10 ? window : 10);   // Non-blocking radio: avoid spinning
            if (_getTime() == before) break;     // Clock is not running at all: do not hang
        }
    }

    --_holdAnnouncements;
}

bool RollCall::isRollCallMessage(const std::string& message) const {
    return startsWith(message, HELLOIAM_PREFIX) ||
           startsWith(message, WHOIS_PREFIX) ||
           startsWith(message, WHEREIS_PREFIX) ||
           startsWith(message, RESPONSE_PREFIX);
}

bool RollCall::processRollCallMessage(const std::string& message, uint16_t srcId) {
    log("[RollCall] Received: " + message + " from ID " + std::to_string(srcId));

    if (startsWith(message, HELLOIAM_PREFIX)) {
        return handleHelloIam(message, srcId);
    } else if (startsWith(message, WHOIS_PREFIX)) {
        return handleWhois(message, srcId);
    } else if (startsWith(message, WHEREIS_PREFIX)) {
        return handleWhereis(message, srcId);
    } else if (startsWith(message, RESPONSE_PREFIX)) {
        return handleResponse(message, srcId);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Validation and parsing. Everything that arrives over the radio is treated
// as untrusted: nothing in here throws or reads out of bounds.
// ---------------------------------------------------------------------------

bool RollCall::isValidName(const std::string& name) {
    if (name.empty() || name.size() > ROLLCALL_MAX_NAME_LEN) return false;
    for (size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20 || c == 0x7F || c == '|') return false;   // no control characters, no separator
    }
    return true;
}

bool RollCall::parseNodeId(const std::string& text, uint16_t& out) {
    if (text.empty() || text.size() > 5) return false;
    uint32_t value = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    if (value == 0 || value >= 0xFFFF) return false;
    out = static_cast<uint16_t>(value);
    return true;
}

bool RollCall::parseAnnouncement(const std::string& content, Announcement& out) {
    // "<name> AT <id>" with an optional " #<nonce>". The name may itself
    // contain " AT ", so split at the last occurrence.
    const size_t atPos = content.rfind(" AT ");
    if (atPos == std::string::npos) return false;

    std::string idPart = content.substr(atPos + 4);
    const size_t noncePos = idPart.find(" #");
    if (noncePos != std::string::npos) {
        const std::string nonceText = idPart.substr(noncePos + 2);
        idPart.erase(noncePos);
        if (nonceText.empty() || nonceText.size() > 5) return false;
        uint32_t nonce = 0;
        for (size_t i = 0; i < nonceText.size(); ++i) {
            const char c = nonceText[i];
            if (c < '0' || c > '9') return false;
            nonce = nonce * 10 + static_cast<uint32_t>(c - '0');
        }
        if (nonce > 0xFFFF) return false;
        out.hasNonce = true;
        out.nonce = static_cast<uint16_t>(nonce);
    }

    out.name = content.substr(0, atPos);
    if (!isValidName(out.name)) return false;
    return parseNodeId(idPart, out.id);
}

bool RollCall::startsWith(const std::string& message, const char* prefix) {
    const size_t n = strlen(prefix);
    return message.size() >= n && message.compare(0, n, prefix) == 0;
}

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

bool RollCall::handleHelloIam(const std::string& message, uint16_t srcId) {
    (void)srcId;
    Announcement a;
    if (!parseAnnouncement(message.substr(strlen(HELLOIAM_PREFIX)), a)) {
        return false;
    }

    const bool sameName = (a.name == _nodeName);
    const bool sameId = (a.id == _nodeId);

    if (sameName && sameId) {
        if (a.hasNonce && a.nonce == _nonce) {
            return true;    // Our own announcement, reflected back by something
        }
        // Another node chose our name and our ID. The node with the larger
        // nonce gives way; announcements without a nonce (version 1 nodes)
        // always win.
        if (!a.hasNonce || _nonce > a.nonce) {
            changeId();
            changeName();
            learn(a.name, a.id);
            scheduleAnnouncement(100, 500);
        } else {
            expediteAnnouncement();
        }
        return true;
    }

    if (sameId) {
        if (_nodeName > a.name) {
            changeId();
            learn(a.name, a.id);
            scheduleAnnouncement(100, 500);
        } else {
            expediteAnnouncement();     // The other node has to move; make sure it hears us
        }
        return true;
    }

    if (sameName) {
        if (_nodeId > a.id) {
            changeName();
            learn(a.name, a.id);
            scheduleAnnouncement(100, 500);
        } else {
            expediteAnnouncement();
        }
        return true;
    }

    const bool isNew = (_nameToId.find(a.name) == _nameToId.end());
    learn(a.name, a.id);
    if (isNew) {
        // Somebody new is on the air: let them learn about us without
        // waiting for the next periodic announcement.
        expediteAnnouncement();
    }
    return true;
}

bool RollCall::handleWhois(const std::string& message, uint16_t srcId) {
    (void)srcId;
    const std::string name = message.substr(strlen(WHOIS_PREFIX));
    if (!isValidName(name)) {
        return false;
    }
    // Only the owner of the name answers. If every node that knows the answer
    // replied, the replies would all collide.
    if (name != _nodeName) {
        return false;
    }
    return send(kBroadcast, std::string(RESPONSE_PREFIX) + _nodeName + " AT " + std::to_string(_nodeId));
}

bool RollCall::handleWhereis(const std::string& message, uint16_t srcId) {
    (void)srcId;
    uint16_t nodeId = 0;
    if (!parseNodeId(message.substr(strlen(WHEREIS_PREFIX)), nodeId)) {
        return false;
    }
    if (nodeId != _nodeId) {
        return false;
    }
    return send(kBroadcast, std::string(RESPONSE_PREFIX) + _nodeName + " AT " + std::to_string(_nodeId));
}

bool RollCall::handleResponse(const std::string& message, uint16_t srcId) {
    (void)srcId;
    Announcement a;
    if (!parseAnnouncement(message.substr(strlen(RESPONSE_PREFIX)), a)) {
        return false;
    }
    // Responses never trigger collision handling (a version 1 node may answer
    // from its cache); learn() ignores anything that claims our name or ID.
    learn(a.name, a.id);
    return true;
}

// ---------------------------------------------------------------------------
// Identity and table management
// ---------------------------------------------------------------------------

void RollCall::registerSelf() {
    _nameToId[_nodeName] = _nodeId;
    _idToName[_nodeId] = _nodeName;
}

void RollCall::learn(const std::string& name, uint16_t nodeId) {
    if (name == _nodeName || nodeId == _nodeId) return;   // Our own entries are not up for grabs

    auto byName = _nameToId.find(name);
    if (byName != _nameToId.end()) {
        if (byName->second == nodeId) return;             // Already known
        _idToName.erase(byName->second);                  // The node changed its ID
    } else if (_nameToId.size() >= ROLLCALL_MAX_PEERS) {
        return;                                           // Table full: ignore newcomers
    }

    auto byId = _idToName.find(nodeId);
    if (byId != _idToName.end()) {
        _nameToId.erase(byId->second);                    // The ID now belongs to another name
    }

    _nameToId[name] = nodeId;
    _idToName[nodeId] = name;
}

void RollCall::changeId() {
    const uint16_t oldId = _nodeId;
    _idToName.erase(oldId);
    _nodeId = generateRandomId();
    _link->setLocalId(_nodeId);
    registerSelf();
    log("[RollCall] ID collision: changed ID from " + std::to_string(oldId) + " to " + std::to_string(_nodeId));
}

void RollCall::changeName() {
    const std::string oldName = _nodeName;
    _nameToId.erase(oldName);
    _nodeName = generateSuffixedName(oldName);
    registerSelf();
    log("[RollCall] Name collision: changed name from " + oldName + " to " + _nodeName);
}

uint16_t RollCall::generateRandomId() {
    // 0 and 0xFFFF are reserved; also avoid IDs we already know to be taken.
    uint16_t candidate = 0;
    for (int tries = 0; tries < 64; ++tries) {
        candidate = _getRandom();
        if (candidate == 0 || candidate == kBroadcast) continue;
        if (candidate == _nodeId) continue;
        if (_idToName.find(candidate) != _idToName.end()) continue;
        return candidate;
    }
    // The random source keeps returning unusable values. Step through the ID
    // space instead of looping forever.
    candidate = _nodeId;
    for (uint32_t i = 0; i < 0xFFFF; ++i) {
        candidate = static_cast<uint16_t>(candidate + 1);
        if (candidate == 0 || candidate == kBroadcast) continue;
        if (_idToName.find(candidate) == _idToName.end()) break;
    }
    return candidate;
}

std::string RollCall::generateSuffixedName(const std::string& base) {
    // Keep room for "-NNNN".
    std::string stem = base;
    if (stem.size() > ROLLCALL_MAX_NAME_LEN - 5) stem.resize(ROLLCALL_MAX_NAME_LEN - 5);

    std::string candidate;
    for (int tries = 0; tries < 64; ++tries) {
        candidate = stem + "-" + std::to_string(_getRandom() % 10000);
        if (_nameToId.find(candidate) == _nameToId.end()) return candidate;
    }
    // Random source is stuck: count upwards until a free name turns up.
    for (uint32_t n = 0; n < 10000; ++n) {
        candidate = stem + "-" + std::to_string(n);
        if (_nameToId.find(candidate) == _nameToId.end()) break;
    }
    return candidate;
}

// ---------------------------------------------------------------------------
// Sending and timing
// ---------------------------------------------------------------------------

bool RollCall::send(uint16_t destId, const std::string& message) {
    if (message.size() > _link->maxPayloadSize()) {
        return false;
    }
    if (destId == kBroadcast) {
        log("[RollCall] Sending: " + message);
    } else {
        log("[RollCall] Sending: " + message + " to ID " + std::to_string(destId));
    }
    return _link->sendPacket(_nodeId, destId,
                             reinterpret_cast<const uint8_t*>(message.data()),
                             static_cast<uint8_t>(message.size()));
}

bool RollCall::broadcastHelloIam() {
    return send(kBroadcast, std::string(HELLOIAM_PREFIX) + _nodeName + " AT " + std::to_string(_nodeId) +
                                " #" + std::to_string(_nonce));
}

void RollCall::scheduleAnnouncement(uint32_t minDelayMs, uint32_t spanMs) {
    _nextAnnounceAt = _getTime() + minDelayMs + (spanMs > 0 ? _getRandom() % spanMs : 0);
}

void RollCall::expediteAnnouncement() {
    const uint32_t now = _getTime();
    const uint32_t soon = now + 200 + _getRandom() % 2800;
    // Only ever move the announcement earlier (the comparison is wrap safe).
    if (static_cast<int32_t>(_nextAnnounceAt - soon) > 0) {
        _nextAnnounceAt = soon;
    }
}

void RollCall::announceIfDue() {
    if (!_started || _holdAnnouncements > 0) return;
    const uint32_t now = _getTime();
    if (static_cast<int32_t>(now - _nextAnnounceAt) < 0) return;

    // Schedule first so that a failed transmission does not retry in a tight loop.
    // The period is randomised: two nodes with the same fixed period that
    // collide once would collide every time.
    scheduleAnnouncement(PERIODIC_ANNOUNCE_INTERVAL_MS - ANNOUNCE_JITTER_MS, 2 * ANNOUNCE_JITTER_MS);
    broadcastHelloIam();
}

void RollCall::listenFor(uint32_t ms) {
    const uint32_t start = _getTime();
    for (;;) {
        const uint32_t before = _getTime();
        const uint32_t elapsed = before - start;
        if (elapsed >= ms) return;
        const uint32_t remaining = ms - elapsed;
        const bool handled = processMessages(remaining);
        if (!handled && _getTime() == before) {
            _sleep(remaining < 20 ? remaining : 20);    // Non-blocking radio: let time pass
            if (_getTime() == before) return;           // Clock is not running at all: do not hang
        }
    }
}

void RollCall::log(const std::string& text) {
    if (_logMessage) {
        _logMessage(text.c_str());
    }
}

// ---------------------------------------------------------------------------
// Default random source
// ---------------------------------------------------------------------------

uint32_t RollCall::createSeedValue() {
    uint32_t seed = 0x9E3779B9u;
#if !defined(ESP_PLATFORM)
#if defined(__cpp_exceptions)
    try {
        std::random_device rd;
        seed ^= rd();
        seed ^= static_cast<uint32_t>(rd()) << 1;
    } catch (...) {
        // No entropy source available on this platform.
    }
#else
    std::random_device rd;
    seed ^= rd();
    seed ^= static_cast<uint32_t>(rd()) << 1;
#endif
#endif
    return seed;
}

std::mt19937 RollCall::_staticRng(RollCall::createSeedValue());

uint16_t RollCall::staticDefaultRandom() {
#if defined(ESP_PLATFORM)
    // Hardware random number generator: different on every device and boot.
    return static_cast<uint16_t>(esp_random());
#else
    return static_cast<uint16_t>(_staticRng());
#endif
}

void RollCall::consoleLog(const char* message) {
    printf("%s\n", message);
}
