#ifndef LORA_BASIC_LINK_H
#define LORA_BASIC_LINK_H

#include "IRadio.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// --- Definitions ---
#define BROADCAST_ADDR 0xFF
#define BUFFER_SIZE    256
#define HEADER_SIZE    5
#define CRC_SIZE       2
#define MAX_PAYLOAD    (BUFFER_SIZE - HEADER_SIZE - CRC_SIZE)

#define FLAG_ACK         0x01
#define FLAG_ACK_REQUEST 0x02

class LoRaBasicLink {
public:
    using time_ms_fn = uint32_t (*)();
    using sleep_ms_fn = void (*)(uint32_t);

    LoRaBasicLink(IRadio* radio, uint8_t localId, time_ms_fn getTime, sleep_ms_fn sleep);

    bool sendPacket(uint8_t destId, const uint8_t* payload, uint8_t len, bool requestAck = false);

    int receivePacket(uint8_t* srcId, uint8_t* buffer, uint8_t maxLen);

private:
    IRadio* _radio;
    uint8_t _localId;
    uint8_t _seqNum;
    time_ms_fn _getTimeMs;
    sleep_ms_fn _sleepMs;

    void sendAck(uint8_t to, uint8_t seq);

    static uint16_t crc16(const uint8_t* data, size_t len);
};

#endif // LORA_BASIC_LINK_H
