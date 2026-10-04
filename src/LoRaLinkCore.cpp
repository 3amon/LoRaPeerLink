/**
 * @file LoRaLinkCore.cpp
 * @brief Shared implementation of the LoRaPeerLink link layer
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * See LoRaLinkCore.h for the frame format and the design notes.
 */

#include "LoRaLinkCore.h"

#include <string.h>

LoRaLinkCore::LoRaLinkCore(IRadio* radio, time_ms_fn getTime, sleep_ms_fn sleep, uint32_t ackTimeoutMs)
    : _radio(radio), _localId(0), _seqNum(0), _getTime(getTime), _sleep(sleep),
      _ackTimeoutMs(ackTimeoutMs), _dupWindowMs(5000), _rngState(0),
      _queueHead(0), _queueCount(0), _seenNext(0) {
    for (uint8_t i = 0; i < LORA_LINK_DUP_TABLE_SIZE; ++i) _seen[i].valid = false;
}

void LoRaLinkCore::setLocalId(uint16_t localId) {
    _localId = localId;
}

// ---------------------------------------------------------------------------
// Frame encoding
// ---------------------------------------------------------------------------

uint16_t LoRaLinkCore::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(static_cast<uint16_t>(data[i]) << 8);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

size_t LoRaLinkCore::encode(uint8_t* out, uint16_t dst, uint16_t src, uint8_t seq, uint8_t flags,
                            const uint8_t* payload, uint8_t len) {
    out[0] = static_cast<uint8_t>(dst >> 8);
    out[1] = static_cast<uint8_t>(dst & 0xFF);
    out[2] = static_cast<uint8_t>(src >> 8);
    out[3] = static_cast<uint8_t>(src & 0xFF);
    out[4] = seq;
    out[5] = flags;
    out[6] = len;
    if (len > 0) memcpy(out + HEADER_SIZE, payload, len);
    const uint16_t crc = crc16(out, static_cast<size_t>(HEADER_SIZE) + len);
    out[HEADER_SIZE + len] = static_cast<uint8_t>(crc >> 8);
    out[HEADER_SIZE + len + 1] = static_cast<uint8_t>(crc & 0xFF);
    return static_cast<size_t>(HEADER_SIZE) + len + CRC_SIZE;
}

bool LoRaLinkCore::decode(const uint8_t* raw, int rawLen, Frame& out) {
    if (rawLen < HEADER_SIZE + CRC_SIZE || rawLen > LORA_MAX_FRAME) return false;
    const uint8_t len = raw[6];
    if (static_cast<int>(len) + HEADER_SIZE + CRC_SIZE != rawLen) return false;
    const uint16_t received = static_cast<uint16_t>((raw[HEADER_SIZE + len] << 8) | raw[HEADER_SIZE + len + 1]);
    if (received != crc16(raw, static_cast<size_t>(HEADER_SIZE) + len)) return false;

    out.dst = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
    out.src = static_cast<uint16_t>((raw[2] << 8) | raw[3]);
    out.seq = raw[4];
    out.flags = raw[5];
    out.len = len;
    out.payload = raw + HEADER_SIZE;
    out.crc = received;
    return true;
}

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------

uint32_t LoRaLinkCore::randomBelow(uint32_t bound) {
    if (bound == 0) return 0;
    // xorshift32, re-stirred with the clock, the node ID and the sequence
    // number so that two nodes running the same firmware drift apart.
    uint32_t x = _rngState ^ (_getTime() * 2654435761u) ^ (static_cast<uint32_t>(_localId) << 16) ^ _seqNum;
    if (x == 0) x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    _rngState = x;
    return (x >> 8) % bound;
}

constexpr uint32_t LoRaLinkCore::DEFAULT_SLOT_MS;
constexpr uint32_t LoRaLinkCore::MAX_BACKOFF_SLOTS;

uint32_t LoRaLinkCore::slotMs(size_t frameLen) {
    const uint32_t frameAir = _radio->timeOnAirMs(frameLen);
    if (frameAir == 0) return DEFAULT_SLOT_MS;
    return frameAir + _radio->timeOnAirMs(HEADER_SIZE + CRC_SIZE) + 20;
}

uint32_t LoRaLinkCore::retryDelayMs(int attempt, uint32_t slot) {
    // Two nodes whose frames collided time out together. Unless they then
    // wait for clearly different times they collide again, so the window has
    // to be several frames long and grow while the channel stays busy.
    uint32_t slots = 2;
    for (int i = 0; i < attempt && slots < MAX_BACKOFF_SLOTS; ++i) slots *= 2;
    if (slots > MAX_BACKOFF_SLOTS) slots = MAX_BACKOFF_SLOTS;
    return randomBelow(slots * slot);
}

uint32_t LoRaLinkCore::effectiveDupWindowMs(size_t frameLen) {
    // Must be longer than the largest gap between two transmissions of the
    // same frame: ACK wait + largest backoff + access delay.
    const uint32_t worstGap = effectiveAckTimeoutMs() + (MAX_BACKOFF_SLOTS + 2) * slotMs(frameLen);
    return worstGap > _dupWindowMs ? worstGap : _dupWindowMs;
}

uint32_t LoRaLinkCore::effectiveAckTimeoutMs() {
    uint32_t timeout = _ackTimeoutMs;
    const uint32_t ackAirtime = _radio->timeOnAirMs(HEADER_SIZE + CRC_SIZE);
    if (ackAirtime > 0) {
        const uint32_t needed = 2 * ackAirtime + 100;
        if (needed > timeout) timeout = needed;
    }
    return timeout;
}

void LoRaLinkCore::listenFor(uint32_t ms) {
    // Used instead of a plain sleep so that frames arriving during a backoff
    // are not lost.
    uint8_t raw[BUFFER_SIZE];
    const uint32_t start = _getTime();
    for (;;) {
        const uint32_t before = _getTime();
        const uint32_t elapsed = before - start;
        if (elapsed >= ms) return;
        const int n = _radio->receive(raw, LORA_MAX_FRAME, ms - elapsed);
        if (n > 0) {
            Frame f;
            if (!decode(raw, n, f)) {
                ++_stats.rxInvalid;
            } else if (!(f.flags & FLAG_ACK)) {
                if (isForUs(f)) queueData(f); else ++_stats.rxForeign;
            }
        } else if (_getTime() == before) {
            // The radio did not block (a non-blocking driver or a test mock):
            // wait out the rest of the delay.
            _sleep(ms - elapsed);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

bool LoRaLinkCore::sendPacket(uint16_t srcId, uint16_t destId, const uint8_t* payload, uint8_t len,
                              bool requestAck, int maxRetries) {
    if (len > MAX_PAYLOAD) return false;
    if (len > 0 && payload == nullptr) return false;

    // Nobody acknowledges a broadcast, so do not wait for one.
    const bool wantAck = requestAck && destId != BROADCAST_ADDR;
    const int attempts = (wantAck && maxRetries > 1) ? maxRetries : 1;

    uint8_t frame[BUFFER_SIZE];
    const uint8_t seq = _seqNum++;
    const size_t frameLen = encode(frame, destId, srcId, seq, wantAck ? FLAG_ACK_REQUEST : 0, payload, len);
    const uint32_t slot = slotMs(frameLen);

    for (int attempt = 0; attempt < attempts; ++attempt) {
        const uint32_t before = preSendDelayMs(attempt, slot);
        if (before > 0) listenFor(before);

        const bool sent = _radio->send(frame, frameLen);
        ++_stats.txFrames;
        if (attempt > 0) ++_stats.txRetransmits;

        if (!wantAck) return sent;
        if (sent && waitForAck(srcId, destId, seq)) {
            ++_stats.rxAcks;
            return true;
        }

        if (attempt + 1 < attempts) {
            const uint32_t after = retryDelayMs(attempt, slot);
            if (after > 0) listenFor(after);
        }
    }

    ++_stats.txFailed;
    return false;
}

bool LoRaLinkCore::waitForAck(uint16_t srcId, uint16_t destId, uint8_t seq) {
    uint8_t raw[BUFFER_SIZE];
    const uint32_t timeout = effectiveAckTimeoutMs();
    const uint32_t start = _getTime();

    for (;;) {
        const uint32_t before = _getTime();
        const uint32_t elapsed = before - start;
        if (elapsed >= timeout) return false;

        // One long window: a windowed radio driver can only hear a frame that
        // fits inside a single receive() call.
        const int n = _radio->receive(raw, LORA_MAX_FRAME, timeout - elapsed);
        if (n > 0) {
            Frame f;
            if (!decode(raw, n, f)) {
                ++_stats.rxInvalid;
            } else if (f.flags & FLAG_ACK) {
                if (f.dst == srcId && f.src == destId && f.seq == seq && f.len == 0) return true;
            } else if (isForUs(f)) {
                // Somebody is talking to us while we wait. Keep the frame for
                // the next receivePacket() call instead of dropping it.
                queueData(f);
            } else {
                ++_stats.rxForeign;
            }
        } else if (_getTime() == before) {
            // Non-blocking radio: poll at a modest rate.
            _sleep(5);
            if (_getTime() == before) {
                // The clock does not advance even across a sleep (broken
                // platform hooks). Give up rather than wait forever.
                return false;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------

int LoRaLinkCore::receivePacket(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen, uint32_t timeoutMs) {
    if (srcId == nullptr || buffer == nullptr) return 0;

    // Frames that arrived while the link was busy sending come first.
    const int queued = popPending(srcId, buffer, maxLen);
    if (queued > 0) return queued;

    uint8_t raw[BUFFER_SIZE];
    const uint32_t start = _getTime();
    uint32_t remaining = timeoutMs;

    for (;;) {
        const int n = _radio->receive(raw, LORA_MAX_FRAME, remaining);
        if (n <= 0) return 0;   // The radio waited out the window (or failed)

        Frame f;
        if (!decode(raw, n, f)) {
            ++_stats.rxInvalid;
        } else if (f.flags & FLAG_ACK) {
            // A stray ACK (late, or for someone else) is never application data.
        } else if (!isForUs(f)) {
            ++_stats.rxForeign;
        } else {
            const int got = acceptData(f, buffer, maxLen);
            if (got >= 0) {
                // An accepted frame with an empty payload also ends the call:
                // it returns 0 with *srcId set.
                *srcId = f.src;
                return got;
            }
        }

        // That frame was not for the application: keep listening for whatever
        // is left of the timeout.
        const uint32_t elapsed = _getTime() - start;
        if (elapsed >= timeoutMs) {
            if (timeoutMs != 0) return 0;
            remaining = 0;      // Zero timeout: keep draining what the radio already has
        } else {
            remaining = timeoutMs - elapsed;
        }
    }
}

int LoRaLinkCore::acceptData(const Frame& f, uint8_t* out, uint8_t maxLen) {
    const bool needsAck = (f.flags & FLAG_ACK_REQUEST) && f.dst == _localId && f.dst != BROADCAST_ADDR;

    if (needsAck && isDuplicate(f, static_cast<size_t>(f.len) + HEADER_SIZE + CRC_SIZE)) {
        // Our ACK was lost and the sender tried again. Acknowledge again, but
        // do not hand the payload to the application a second time.
        ++_stats.rxDuplicates;
        sendAck(f);
        return -1;
    }

    if (f.len > maxLen) {
        // Not acknowledged: the sender should not believe this was delivered.
        ++_stats.rxDropped;
        return -1;
    }

    if (f.len > 0) memcpy(out, f.payload, f.len);
    ++_stats.rxFrames;
    if (needsAck) {
        remember(f);
        sendAck(f);
    }
    return f.len;
}

void LoRaLinkCore::queueData(const Frame& f) {
    if (_queueCount >= LORA_LINK_RX_QUEUE_SIZE) {
        // No room. Stay silent so the sender retries later.
        ++_stats.rxDropped;
        return;
    }
    Pending& slot = _queue[(_queueHead + _queueCount) % LORA_LINK_RX_QUEUE_SIZE];
    const int got = acceptData(f, slot.data, MAX_PAYLOAD);
    if (got > 0) {
        slot.src = f.src;
        slot.len = static_cast<uint8_t>(got);
        ++_queueCount;
    }
}

int LoRaLinkCore::popPending(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen) {
    while (_queueCount > 0) {
        Pending& slot = _queue[_queueHead];
        _queueHead = static_cast<uint8_t>((_queueHead + 1) % LORA_LINK_RX_QUEUE_SIZE);
        --_queueCount;
        if (slot.len > maxLen) {
            ++_stats.rxDropped;
            continue;
        }
        memcpy(buffer, slot.data, slot.len);
        *srcId = slot.src;
        return slot.len;
    }
    return 0;
}

bool LoRaLinkCore::isDuplicate(const Frame& f, size_t frameLen) {
    const uint32_t now = _getTime();
    const uint32_t window = effectiveDupWindowMs(frameLen);
    for (uint8_t i = 0; i < LORA_LINK_DUP_TABLE_SIZE; ++i) {
        Seen& s = _seen[i];
        if (!s.valid) continue;
        if (now - s.timeMs > window) {
            s.valid = false;
            continue;
        }
        if (s.src == f.src && s.seq == f.seq && s.crc == f.crc) {
            s.timeMs = now;     // Sliding window: retransmissions keep the entry alive
            return true;
        }
    }
    return false;
}

void LoRaLinkCore::remember(const Frame& f) {
    // Prefer a free slot, otherwise overwrite the oldest entry (round robin).
    uint8_t index = _seenNext;
    for (uint8_t i = 0; i < LORA_LINK_DUP_TABLE_SIZE; ++i) {
        if (!_seen[i].valid) {
            index = i;
            break;
        }
    }
    if (index == _seenNext) _seenNext = static_cast<uint8_t>((_seenNext + 1) % LORA_LINK_DUP_TABLE_SIZE);
    Seen& s = _seen[index];
    s.src = f.src;
    s.seq = f.seq;
    s.crc = f.crc;
    s.timeMs = _getTime();
    s.valid = true;
}

void LoRaLinkCore::sendAck(const Frame& f) {
    uint8_t frame[HEADER_SIZE + CRC_SIZE];
    const size_t n = encode(frame, f.src, _localId, f.seq, FLAG_ACK, nullptr, 0);
    _radio->send(frame, n);
    ++_stats.txAcks;
}
