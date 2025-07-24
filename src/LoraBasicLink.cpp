/**
 * @file LoraBasicLink.cpp
 * @brief Implementation of basic LoRa link layer protocol
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements the LoRaBasicLink class, providing a straightforward
 * LoRa link layer protocol with packet framing, CRC validation, and optional
 * acknowledgment mechanisms for reliable peer-to-peer communication.
 */

#include "LoraBasicLink.h"
#include <string.h>

/**
 * @brief Constructor initializes the basic link with radio and configuration
 * 
 * Sets up the link layer with the provided radio interface and node configuration.
 * The sequence number is initialized to 0 and will increment with each sent packet.
 */
LoRaBasicLink::LoRaBasicLink(IRadio* radio, time_ms_fn getTime, sleep_ms_fn sleep)
    : _radio(radio), _localId(0), _seqNum(0),
      _getTimeMs(getTime), _sleepMs(sleep) {}

void LoRaBasicLink::setLocalId(uint16_t localId) {
    _localId = localId;
}

/**
 * @brief Send a packet with optional acknowledgment and retry logic
 * 
 * Constructs a complete packet with header and CRC, then transmits it.
 * If acknowledgment is requested, waits for ACK response and returns success
 * only if ACK is received within timeout (500ms).
 * 
 * Packet format: [DestID:16][SrcID:16][SeqNum:8][Flags:8][PayloadLen:8][Payload...][CRC16]
 * 
 * The method handles:
 * - Payload size validation
 * - Sequence number management
 * - CRC calculation and appending
 * - ACK timeout and matching
 */
bool LoRaBasicLink::sendPacket(uint16_t srcId, uint16_t destId, const uint8_t* payload, uint8_t len, bool requestAck, int maxRetries) {
    // Validate payload size against maximum allowed
    if (len > MAX_PAYLOAD) return false;

    // Construct packet with header (16-bit addressing)
    uint8_t buffer[BUFFER_SIZE];
    buffer[0] = destId >> 8;                               // Destination ID high byte
    buffer[1] = destId & 0xFF;                             // Destination ID low byte
    buffer[2] = srcId >> 8;                                // Source ID high byte  
    buffer[3] = srcId & 0xFF;                              // Source ID low byte
    buffer[4] = _seqNum++;                                 // Sequence number (auto-increment)
    buffer[5] = requestAck ? FLAG_ACK_REQUEST : 0;         // Flags
    buffer[6] = len;                                       // Payload length
    memcpy(&buffer[7], payload, len);                      // Copy payload data

    // Calculate and append CRC for the header + payload  
    uint16_t crc = crc16(buffer, 7 + len);
    buffer[7 + len] = crc >> 8;      // CRC high byte
    buffer[8 + len] = crc & 0xFF;    // CRC low byte

    // Transmit the complete packet
    bool ok = _radio->send(buffer, 9 + len);

    // If ACK requested and transmission was successful, wait for acknowledgment
    if (requestAck && ok) {
        uint8_t rxBuf[BUFFER_SIZE];
        uint32_t start = _getTimeMs();
        
        // Wait up to 500ms for ACK response
        while (_getTimeMs() - start < 500) {
            int received = _radio->receive(rxBuf, BUFFER_SIZE, 20);
            
            // Check if received packet is a valid ACK for our transmission (with 16-bit addressing)
            if (received >= 9 &&                           // Minimum packet size (7 byte header + 2 byte CRC)
                rxBuf[5] & FLAG_ACK &&                     // ACK flag set (flags at position 5)
                rxBuf[4] == buffer[4]) {                   // Sequence number matches (seq at position 4)
                return true;
            }
            _sleepMs(5); // Brief pause before next check
        }
        return false; // Timeout - no ACK received
    }

    return ok; // Return transmission result if no ACK required
}

/**
 * @brief Receive and process incoming packets
 * 
 * Checks for incoming packets, validates them, and processes according to protocol:
 * - Filters packets not addressed to this node (unless broadcast)
 * - Validates packet structure and CRC integrity
 * - Automatically sends ACK if requested by sender
 * - Returns payload data for valid packets
 * - Respects timeout parameter for waiting for packets
 * 
 * The method performs comprehensive packet validation including:
 * - Minimum packet size check
 * - Address filtering (unicast to this node or broadcast)
 * - Payload length consistency verification
 * - CRC integrity validation
 */
int LoRaBasicLink::receivePacket(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen, uint32_t timeoutMs) {
    uint8_t raw[BUFFER_SIZE];
    int len = _radio->receive(raw, BUFFER_SIZE, timeoutMs);
    
    // Minimum packet size check (header + CRC = 9 bytes with 16-bit addressing)
    if (len < 9) return 0;

    // Extract header fields (16-bit addressing)
    uint16_t dstId = (raw[0] << 8) | raw[1];        // Destination ID
    *srcId = (raw[2] << 8) | raw[3];                // Source ID
    uint8_t seq = raw[4];                           // Sequence number
    uint8_t flags = raw[5];                         // Protocol flags
    uint8_t payloadLen = raw[6];                    // Payload length

    // Address filtering: only process packets for this node or broadcasts
    if (dstId != _localId && dstId != BROADCAST_ADDR) return 0;
    
    // Validate packet structure: payload length must match actual packet size
    if (payloadLen + 9 != len) return 0;

    // Extract and verify CRC
    uint16_t receivedCrc = (raw[7 + payloadLen] << 8) | raw[8 + payloadLen];
    if (receivedCrc != crc16(raw, 7 + payloadLen)) return 0;

    // Copy payload to output buffer (respect buffer limits)
    memcpy(buffer, &raw[7], (payloadLen > maxLen ? maxLen : payloadLen));

    // Send automatic ACK if requested and this is a unicast packet
    if ((flags & FLAG_ACK_REQUEST) && dstId == _localId) {
        sendAck(_localId, *srcId, seq);
    }

    return payloadLen;
}

/**
 * @brief Send acknowledgment packet
 * 
 * Constructs and sends a minimal ACK packet in response to a received packet
 * that requested acknowledgment. The ACK contains the original sequence number
 * to allow the sender to match it with the original transmission.
 * 
 * ACK packet format: [DestID:16][SrcID:16][SeqNum:8][FLAG_ACK:8][0:8][CRC16]
 * The payload length is 0 since ACKs carry no additional data.
 */
void LoRaBasicLink::sendAck(uint16_t srcId, uint16_t to, uint8_t seq) {
    uint8_t buffer[9];
    buffer[0] = to >> 8;          // Send ACK back to original sender (high byte)
    buffer[1] = to & 0xFF;        // Send ACK back to original sender (low byte)
    buffer[2] = srcId >> 8;       // ACK comes from this node (high byte)
    buffer[3] = srcId & 0xFF;     // ACK comes from this node (low byte)
    buffer[4] = seq;              // Echo the sequence number being acknowledged
    buffer[5] = FLAG_ACK;         // Set ACK flag
    buffer[6] = 0;                // No payload in ACK packets
    
    // Calculate and append CRC for ACK packet
    uint16_t crc = crc16(buffer, 7);
    buffer[7] = crc >> 8;         // CRC high byte
    buffer[8] = crc & 0xFF;       // CRC low byte
    
    // Send the ACK packet (no need to wait for ACK of an ACK)
    _radio->send(buffer, 9);
}

/**
 * @brief Calculate 16-bit CRC using CCITT polynomial
 * 
 * Implements CRC-16-CCITT algorithm with polynomial 0x1021 for error detection.
 * This is a standard CRC used in many communication protocols and provides
 * good error detection capabilities for the packet sizes used in LoRa communication.
 * 
 * The algorithm:
 * 1. Initialize CRC to 0xFFFF
 * 2. For each byte, XOR with CRC shifted left 8 bits
 * 3. For each bit, check MSB and either shift left or shift left and XOR with polynomial
 * 4. Return final CRC value
 */
uint16_t LoRaBasicLink::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF; // Initialize with all 1s
    
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8; // XOR data byte into high byte of CRC
        
        // Process each bit
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {        // Check MSB
                crc = (crc << 1) ^ 0x1021; // Shift and XOR with polynomial
            } else {
                crc = crc << 1;        // Just shift left
            }
        }
    }
    
    return crc;
}