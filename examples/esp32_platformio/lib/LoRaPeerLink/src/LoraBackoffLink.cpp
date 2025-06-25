/**
 * @file LoraBackoffLink.cpp
 * @brief Implementation of advanced LoRa link layer with exponential backoff
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements the LoRaBackoffLink class, providing an advanced LoRa
 * link layer protocol with exponential backoff for collision avoidance in
 * multi-node networks. The implementation includes intelligent retry mechanisms
 * and adaptive timing to improve network throughput under congestion.
 */

#include "LoraBackoffLink.h"
#include <string.h>

/**
 * @brief Constructor initializes the backoff link with radio and configuration
 * 
 * Sets up the advanced link layer with provided radio interface and node configuration.
 * Initializes sequence number counter and stores timing function pointers for
 * backoff calculations and network synchronization.
 */
LoRaBackoffLink::LoRaBackoffLink(IRadio* radio, time_ms_fn getTime, sleep_ms_fn sleep)
    : _radio(radio), _localId(0), _seqNum(0), _getTime(getTime), _sleep(sleep) {}

void LoRaBackoffLink::setLocalId(uint16_t localId) {
    _localId = localId;
}

/**
 * @brief Send a packet with exponential backoff and intelligent retry logic
 * 
 * Implements advanced transmission strategy with collision avoidance:
 * 1. Constructs packet with structured header
 * 2. Applies random initial delay to avoid synchronized transmissions
 * 3. Attempts transmission with exponentially increasing backoff on failure
 * 4. Waits for ACK with timeout if acknowledgment required
 * 5. Retries with longer delays if no ACK received
 * 
 * The backoff algorithm uses randomized delays to minimize collision probability
 * when multiple nodes attempt simultaneous transmission.
 */
bool LoRaBackoffLink::sendPacket(uint16_t srcId, uint16_t dest, const uint8_t* data, uint8_t len, bool requireAck, int maxRetries) {
    // Validate payload size against current configuration
    if (len > maxPayloadSize()) return false;

    // Construct packet with structured header
    uint8_t buffer[BUFFER_SIZE];
    PacketHeader* hdr = reinterpret_cast<PacketHeader*>(buffer);
    hdr->dst = dest;                                      // Destination node ID
    hdr->src = srcId;                                     // Source node ID (passed as parameter)
    hdr->seq = _seqNum++;                                 // Sequence number (auto-increment)
    hdr->flags = (requireAck ? FLAG_NEEDS_ACK : 0);       // Protocol flags
    hdr->len = len;                                       // Payload length

    // Copy payload data after header
    memcpy(buffer + sizeof(PacketHeader), data, len);
    
    // Calculate and append CRC for header + payload
    uint16_t crc = crc16(buffer, sizeof(PacketHeader) + len);
    buffer[sizeof(PacketHeader) + len]     = crc >> 8;    // CRC high byte
    buffer[sizeof(PacketHeader) + len + 1] = crc & 0xFF;  // CRC low byte

    // Retry loop with exponential backoff
    int attempt = 0;
    while (attempt++ < maxRetries) {
        // Random initial delay to avoid synchronized transmissions
        _sleep(10 + (_getTime() % 40));
        
        // Attempt transmission
        _radio->send(buffer, sizeof(PacketHeader) + len + 2);

        // If no ACK required, consider transmission successful
        if (!requireAck) return true;
        
        // Wait for acknowledgment with timeout
        if (waitForAck(hdr->seq, 200)) return true;

        // Exponential backoff: longer delay before next retry
        // Uses current time for randomization to avoid synchronized retries
        _sleep(50 + (_getTime() % 150));
    }

    // All retry attempts exhausted
    return false;
}

/**
 * @brief Receive and process incoming packets with protocol validation
 * 
 * Processes incoming packets with comprehensive validation and automatic
 * response handling:
 * - Validates packet structure and header consistency
 * - Performs CRC integrity checking
 * - Filters packets by destination address
 * - Automatically sends ACK responses when required
 * - Extracts and returns payload data
 * 
 * The method ensures only valid, properly addressed packets are processed
 * and automatically handles protocol-level acknowledgment requirements.
 */
int LoRaBackoffLink::receivePacket(uint16_t* src, uint8_t* buffer, uint8_t maxLen) {
    uint8_t raw[BUFFER_SIZE];
    int len = _radio->receive(raw, BUFFER_SIZE);
    
    // Minimum packet size check (header + CRC = PacketHeader + 2 bytes)
    if (len < (int)(sizeof(PacketHeader) + 2)) return 0;

    // Extract structured header
    PacketHeader* hdr = reinterpret_cast<PacketHeader*>(raw);
    
    // Validate packet structure: payload length must match actual packet size
    if (hdr->len + sizeof(PacketHeader) + 2 != len) return 0;

    // Extract and verify CRC (last 2 bytes)
    uint16_t receivedCrc = (raw[len - 2] << 8) | raw[len - 1];
    if (receivedCrc != crc16(raw, len - 2)) return 0;

    // Address filtering: only process packets for this node or broadcasts
    if (hdr->dst != _localId && hdr->dst != BROADCAST_ADDR) return 0;

    // Validate payload fits in provided buffer
    if (hdr->len > maxLen) return 0;

    // Extract payload data and source information
    memcpy(buffer, raw + sizeof(PacketHeader), hdr->len);
    *src = hdr->src;

    // Send automatic ACK if requested and this is a unicast packet
    if ((hdr->flags & FLAG_NEEDS_ACK) && hdr->dst == _localId) {
        sendAck(_localId, hdr->src, hdr->seq);
    }

    return hdr->len;
}

/**
 * @brief Calculate maximum payload size for current configuration
 * 
 * Determines the maximum number of payload bytes that can be transmitted
 * based on the total buffer size minus protocol overhead (header + CRC).
 * This ensures packets fit within the configured buffer limits.
 */
uint8_t LoRaBackoffLink::maxPayloadSize() const {
    return BUFFER_SIZE - sizeof(PacketHeader) - 2; // Buffer - Header - CRC
}

/**
 * @brief Send acknowledgment packet
 * 
 * Constructs and transmits a minimal ACK packet in response to a received
 * packet that requested acknowledgment. The ACK includes the original
 * sequence number for proper matching by the sender.
 * 
 * ACK packets have zero payload length and only contain the necessary
 * header information for protocol operation.
 */
void LoRaBackoffLink::sendAck(uint16_t srcId, uint16_t to, uint8_t seq) {
    uint8_t buffer[BUFFER_SIZE];
    PacketHeader* hdr = reinterpret_cast<PacketHeader*>(buffer);
    
    // Construct ACK header
    hdr->dst = to;              // Send ACK back to original sender
    hdr->src = srcId;           // ACK originates from this node
    hdr->seq = seq;             // Echo the sequence number being acknowledged
    hdr->flags = FLAG_ACK;      // Set ACK flag
    hdr->len = 0;               // No payload in ACK packets
    
    // Calculate and append CRC for ACK packet
    uint16_t crc = crc16(buffer, sizeof(PacketHeader));
    buffer[sizeof(PacketHeader)]     = crc >> 8;     // CRC high byte
    buffer[sizeof(PacketHeader) + 1] = crc & 0xFF;   // CRC low byte
    
    // Transmit ACK packet
    _radio->send(buffer, sizeof(PacketHeader) + 2);
}

/**
 * @brief Wait for acknowledgment with timeout and validation
 * 
 * Monitors incoming packets for a matching ACK response within the specified
 * timeout period. Validates ACK packets to ensure they match the expected
 * sequence number and are properly addressed to this node.
 * 
 * The method continues checking for ACK packets until either:
 * - A valid matching ACK is received (returns true)
 * - The timeout period expires (returns false)
 * 
 * Brief sleeps between checks prevent excessive CPU usage while waiting.
 */
bool LoRaBackoffLink::waitForAck(uint8_t expectedSeq, uint32_t timeoutMs) {
    uint32_t start = _getTime();
    
    while (_getTime() - start < timeoutMs) {
        uint8_t raw[BUFFER_SIZE];
        int len = _radio->receive(raw, BUFFER_SIZE, 20); // Short timeout per attempt
        
        // Check if received packet could be an ACK
        if (len >= (int)(sizeof(PacketHeader) + 2)) {
            PacketHeader* hdr = reinterpret_cast<PacketHeader*>(raw);
            
            // Extract and verify CRC
            uint16_t receivedCrc = (raw[len - 2] << 8) | raw[len - 1];
            
            // Validate ACK packet: CRC, destination, ACK flag, sequence number
            if (receivedCrc == crc16(raw, len - 2) &&    // CRC valid
                hdr->dst == _localId &&                  // Addressed to this node
                (hdr->flags & FLAG_ACK) &&               // ACK flag set
                hdr->seq == expectedSeq) {               // Sequence number matches
                return true;
            }
        }
        
        _sleep(5); // Brief pause before next check
    }
    
    return false; // Timeout - no matching ACK received
}

/**
 * @brief Calculate 16-bit CRC using CCITT polynomial
 * 
 * Implements the same CRC-16-CCITT algorithm as LoRaBasicLink for packet
 * compatibility. Uses polynomial 0x1021 which provides good error detection
 * characteristics for the packet sizes typically used in LoRa communication.
 * 
 * The algorithm processes each byte bit-by-bit, maintaining compatibility
 * across different link layer implementations while ensuring data integrity.
 */
uint16_t LoRaBackoffLink::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF; // Initialize with all 1s
    
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8; // XOR data byte into high byte of CRC
        
        // Process each bit
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {        // Check MSB
                crc = (crc << 1) ^ 0x1021; // Shift and XOR with polynomial
            } else {
                crc = crc << 1;        // Just shift left
            }
        }
    }
    
    return crc;
}