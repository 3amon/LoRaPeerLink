#ifndef LORA_BACKOFF_LINK_H
#define LORA_BACKOFF_LINK_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "IRadio.h"
#include "ILoRaLink.h"

#define BROADCAST_ADDR 0xFF
#define BUFFER_SIZE    256

class LoRaBackoffLink : public ILoRaLink {
public:
    using time_ms_fn = uint32_t (*)();
    using sleep_ms_fn = void (*)(uint32_t);

    LoRaBackoffLink(IRadio* radio, uint8_t nodeId, time_ms_fn getTime, sleep_ms_fn sleep)
        : _radio(radio), _nodeId(nodeId), _seqNum(0), _getTime(getTime), _sleep(sleep) {}

    bool sendPacket(uint8_t dest, const uint8_t* data, uint8_t len, bool requireAck = false, int maxRetries = 3) override {
        if (len > maxPayloadSize()) return false;

        uint8_t buffer[BUFFER_SIZE];
        PacketHeader* hdr = reinterpret_cast<PacketHeader*>(buffer);
        hdr->dst = dest;
        hdr->src = _nodeId;
        hdr->seq = _seqNum++;
        hdr->flags = (requireAck ? FLAG_NEEDS_ACK : 0);
        hdr->len = len;

        memcpy(buffer + sizeof(PacketHeader), data, len);
        uint16_t crc = crc16(buffer, sizeof(PacketHeader) + len);
        buffer[sizeof(PacketHeader) + len]     = crc >> 8;
        buffer[sizeof(PacketHeader) + len + 1] = crc & 0xFF;

        int attempt = 0;
        while (attempt++ < maxRetries) {
            _sleep(10 + (_getTime() % 40));
            _radio->send(buffer, sizeof(PacketHeader) + len + 2);

            if (!requireAck) return true;
            if (waitForAck(hdr->seq, 200)) return true;

            _sleep(50 + (_getTime() % 150));
        }

        return false;
    }

    int receivePacket(uint8_t* src, uint8_t* buffer, uint8_t maxLen) override {
        uint8_t raw[BUFFER_SIZE];
        int len = _radio->receive(raw, BUFFER_SIZE);
        if (len < (int)(sizeof(PacketHeader) + 2)) return 0;

        PacketHeader* hdr = reinterpret_cast<PacketHeader*>(raw);
        if (hdr->len + sizeof(PacketHeader) + 2 != len) return 0;

        uint16_t crc = (raw[len - 2] << 8) | raw[len - 1];
        if (crc != crc16(raw, len - 2)) return 0;

        if (hdr->dst != _nodeId && hdr->dst != BROADCAST_ADDR) return 0;

        if (hdr->len > maxLen) return 0;

        memcpy(buffer, raw + sizeof(PacketHeader), hdr->len);
        *src = hdr->src;

        if ((hdr->flags & FLAG_NEEDS_ACK) && hdr->dst == _nodeId) {
            sendAck(hdr->src, hdr->seq);
        }

        return hdr->len;
    }

private:
    IRadio* _radio;
    uint8_t _nodeId;
    uint8_t _seqNum;
    time_ms_fn _getTime;
    sleep_ms_fn _sleep;

    enum Flags : uint8_t {
        FLAG_ACK        = 0x01,
        FLAG_NEEDS_ACK  = 0x02
    };

    struct __attribute__((packed)) PacketHeader {
        uint8_t dst;
        uint8_t src;
        uint8_t seq;
        uint8_t flags;
        uint8_t len;
    };

    uint8_t maxPayloadSize() const {
        return BUFFER_SIZE - sizeof(PacketHeader) - 2;
    }

    void sendAck(uint8_t to, uint8_t seq) {
        uint8_t buffer[BUFFER_SIZE];
        PacketHeader* hdr = reinterpret_cast<PacketHeader*>(buffer);
        hdr->dst = to;
        hdr->src = _nodeId;
        hdr->seq = seq;
        hdr->flags = FLAG_ACK;
        hdr->len = 0;
        uint16_t crc = crc16(buffer, sizeof(PacketHeader));
        buffer[sizeof(PacketHeader)]     = crc >> 8;
        buffer[sizeof(PacketHeader) + 1] = crc & 0xFF;
        _radio->send(buffer, sizeof(PacketHeader) + 2);
    }

    bool waitForAck(uint8_t expectedSeq, uint32_t timeoutMs) {
        uint32_t start = _getTime();
        while (_getTime() - start < timeoutMs) {
            uint8_t raw[BUFFER_SIZE];
            int len = _radio->receive(raw, BUFFER_SIZE, 20);
            if (len >= (int)(sizeof(PacketHeader) + 2)) {
                PacketHeader* hdr = reinterpret_cast<PacketHeader*>(raw);
                uint16_t crc = (raw[len - 2] << 8) | raw[len - 1];
                if (crc == crc16(raw, len - 2) &&
                    hdr->dst == _nodeId &&
                    (hdr->flags & FLAG_ACK) &&
                    hdr->seq == expectedSeq) {
                    return true;
                }
            }
            _sleep(5);
        }
        return false;
    }

    uint16_t crc16(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; ++i) {
            crc ^= (uint16_t)data[i] << 8;
            for (int j = 0; j < 8; j++) {
                crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
            }
        }
        return crc;
    }
};

#endif // LORA_BACKOFF_LINK_H
