#include "LoraBackoffLink.h"
#include <string.h>

LoRaBackoffLink::LoRaBackoffLink(IRadio* radio, uint8_t nodeId, time_ms_fn getTime, sleep_ms_fn sleep)
    : _radio(radio), _nodeId(nodeId), _seqNum(0), _getTime(getTime), _sleep(sleep) {}

bool LoRaBackoffLink::sendPacket(uint8_t dest, const uint8_t* data, uint8_t len, bool requireAck, int maxRetries) {
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

int LoRaBackoffLink::receivePacket(uint8_t* src, uint8_t* buffer, uint8_t maxLen) {
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

uint8_t LoRaBackoffLink::maxPayloadSize() const {
    return BUFFER_SIZE - sizeof(PacketHeader) - 2;
}

void LoRaBackoffLink::sendAck(uint8_t to, uint8_t seq) {
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

bool LoRaBackoffLink::waitForAck(uint8_t expectedSeq, uint32_t timeoutMs) {
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

uint16_t LoRaBackoffLink::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}