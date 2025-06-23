#ifndef LORA_BACKOFF_LINK_H
#define LORA_BACKOFF_LINK_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "IRadio.h"

#define BROADCAST_ADDR 0xFF
#define BUFFER_SIZE    256

class LoRaBackoffLink {
public:
    using time_ms_fn = uint32_t (*)();
    using sleep_ms_fn = void (*)(uint32_t);

    LoRaBackoffLink(IRadio* radio, uint8_t nodeId, time_ms_fn getTime, sleep_ms_fn sleep);

    bool sendPacket(uint8_t dest, const uint8_t* data, uint8_t len, bool requireAck = false, int maxRetries = 3);

    int receivePacket(uint8_t* src, uint8_t* buffer, uint8_t maxLen);

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

    uint8_t maxPayloadSize() const;

    void sendAck(uint8_t to, uint8_t seq);

    bool waitForAck(uint8_t expectedSeq, uint32_t timeoutMs);

    uint16_t crc16(const uint8_t* data, size_t len);
};

#endif // LORA_BACKOFF_LINK_H
