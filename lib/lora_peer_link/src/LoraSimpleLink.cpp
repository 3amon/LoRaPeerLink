#include "LoraSimpleLink.h"
#include <string.h>

LoRaBasicLink::LoRaBasicLink(IRadio* radio, uint8_t localId, time_ms_fn getTime, sleep_ms_fn sleep)
    : _radio(radio), _localId(localId), _seqNum(0),
      _getTimeMs(getTime), _sleepMs(sleep) {}

bool LoRaBasicLink::sendPacket(uint8_t destId, const uint8_t* payload, uint8_t len, bool requestAck) {
    if (len > MAX_PAYLOAD) return false;

    uint8_t buffer[BUFFER_SIZE];
    buffer[0] = destId;
    buffer[1] = _localId;
    buffer[2] = _seqNum++;
    buffer[3] = requestAck ? FLAG_ACK_REQUEST : 0;
    buffer[4] = len;
    memcpy(&buffer[5], payload, len);

    uint16_t crc = crc16(buffer, 5 + len);
    buffer[5 + len] = crc >> 8;
    buffer[6 + len] = crc & 0xFF;

    bool ok = _radio->send(buffer, 7 + len);

    if (requestAck && ok) {
        uint8_t rxBuf[BUFFER_SIZE];
        uint32_t start = _getTimeMs();
        while (_getTimeMs() - start < 500) {
            int received = _radio->receive(rxBuf, BUFFER_SIZE, 20);
            if (received >= 7 &&
                rxBuf[3] & FLAG_ACK &&
                rxBuf[2] == buffer[2]) {
                return true;
            }
            _sleepMs(5);
        }
        return false;
    }

    return ok;
}

int LoRaBasicLink::receivePacket(uint8_t* srcId, uint8_t* buffer, uint8_t maxLen) {
    uint8_t raw[BUFFER_SIZE];
    int len = _radio->receive(raw, BUFFER_SIZE);
    if (len < 7) return 0;

    uint8_t dstId = raw[0];
    *srcId = raw[1];
    uint8_t seq = raw[2];
    uint8_t flags = raw[3];
    uint8_t payloadLen = raw[4];

    if (dstId != _localId && dstId != BROADCAST_ADDR) return 0;
    if (payloadLen + 7 != len) return 0;

    uint16_t crc = (raw[5 + payloadLen] << 8) | raw[6 + payloadLen];
    if (crc != crc16(raw, 5 + payloadLen)) return 0;

    memcpy(buffer, &raw[5], (payloadLen > maxLen ? maxLen : payloadLen));

    if ((flags & FLAG_ACK_REQUEST) && dstId == _localId) {
        sendAck(*srcId, seq);
    }

    return payloadLen;
}

void LoRaBasicLink::sendAck(uint8_t to, uint8_t seq) {
    uint8_t buffer[7];
    buffer[0] = to;
    buffer[1] = _localId;
    buffer[2] = seq;
    buffer[3] = FLAG_ACK;
    buffer[4] = 0;
    uint16_t crc = crc16(buffer, 5);
    buffer[5] = crc >> 8;
    buffer[6] = crc & 0xFF;
    _radio->send(buffer, 7);
}

uint16_t LoRaBasicLink::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}