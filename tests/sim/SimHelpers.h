/**
 * @file SimHelpers.h
 * @brief Small helpers shared by the simulator-based tests.
 */
#ifndef SIM_HELPERS_H
#define SIM_HELPERS_H

#include "sim/SimRadio.h"
#include "ILoRaLink.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace simtest {

/** A payload handed to the application by a link. */
struct Rx {
    uint16_t src;
    std::string payload;
    uint32_t atMs;
};

/** Receive on @p link until virtual time @p untilMs, collecting every payload. */
inline void listenUntil(ILoRaLink& link, uint32_t untilMs, std::vector<Rx>& out, uint32_t windowMs = 1000) {
    uint8_t buf[255];
    uint16_t src = 0;
    while (sim::nowMs() < untilMs) {
        uint32_t remaining = untilMs - sim::nowMs();
        int n = link.receivePacket(&src, buf, 255, std::min(remaining, windowMs));
        if (n > 0) out.push_back({src, std::string(reinterpret_cast<char*>(buf), static_cast<size_t>(n)), sim::nowMs()});
    }
}

inline bool sendText(ILoRaLink& link, uint16_t src, uint16_t dst, const std::string& text,
                     bool ack = false, int maxRetries = 3) {
    return link.sendPacket(src, dst, reinterpret_cast<const uint8_t*>(text.data()),
                           static_cast<uint8_t>(text.size()), ack, maxRetries);
}

inline int countPayload(const std::vector<Rx>& rx, const std::string& payload) {
    return static_cast<int>(std::count_if(rx.begin(), rx.end(), [&](const Rx& r) { return r.payload == payload; }));
}

/** CRC-16/CCITT-FALSE, the checksum used by the link layer frames. */
inline uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int j = 0; j < 8; ++j) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

const uint8_t FLAG_ACK = 0x01;
const uint8_t FLAG_ACK_REQUEST = 0x02;

/**
 * Build a raw link layer frame exactly as documented:
 * [dst:16 BE][src:16 BE][seq][flags][len][payload][crc16 BE]
 */
inline std::vector<uint8_t> frame(uint16_t dst, uint16_t src, uint8_t seq, uint8_t flags, const std::string& payload = "") {
    std::vector<uint8_t> f;
    f.push_back(static_cast<uint8_t>(dst >> 8));
    f.push_back(static_cast<uint8_t>(dst & 0xFF));
    f.push_back(static_cast<uint8_t>(src >> 8));
    f.push_back(static_cast<uint8_t>(src & 0xFF));
    f.push_back(seq);
    f.push_back(flags);
    f.push_back(static_cast<uint8_t>(payload.size()));
    f.insert(f.end(), payload.begin(), payload.end());
    uint16_t crc = crc16(f.data(), f.size());
    f.push_back(static_cast<uint8_t>(crc >> 8));
    f.push_back(static_cast<uint8_t>(crc & 0xFF));
    return f;
}

/** Parsed view of a raw frame seen on the air (no CRC check). */
struct FrameView {
    uint16_t dst = 0, src = 0;
    uint8_t seq = 0, flags = 0, len = 0;
    bool valid = false;
};

inline FrameView parse(const std::vector<uint8_t>& b) {
    FrameView v;
    if (b.size() < 9) return v;
    v.dst = static_cast<uint16_t>((b[0] << 8) | b[1]);
    v.src = static_cast<uint16_t>((b[2] << 8) | b[3]);
    v.seq = b[4];
    v.flags = b[5];
    v.len = b[6];
    v.valid = (static_cast<size_t>(v.len) + 9 == b.size());
    return v;
}

} // namespace simtest

#endif // SIM_HELPERS_H
