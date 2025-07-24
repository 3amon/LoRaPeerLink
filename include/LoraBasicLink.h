/**
 * @file LoraBasicLink.h
 * @brief Basic LoRa link layer implementation with simple packet transmission
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements a basic LoRa link layer protocol that provides:
 * - Simple packet framing with headers and CRC validation
 * - Optional acknowledgment mechanism
 * - Basic retry logic for reliable transmission
 * - Broadcast and unicast messaging support
 * 
 * The protocol uses a 5-byte header followed by payload and 2-byte CRC:
 * [DestID][SrcID][SeqNum][Flags][PayloadLen][Payload...][CRC16]
 */

#ifndef LORA_BASIC_LINK_H
#define LORA_BASIC_LINK_H

#include "IRadio.h"
#include "ILoRaLink.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// --- Protocol Definitions ---
#define BROADCAST_ADDR 0xFFFF      ///< Address for broadcast messages (16-bit)
#define BUFFER_SIZE    256         ///< Maximum packet size including headers
#define HEADER_SIZE    7           ///< Size of packet header in bytes (2 bytes each for dst/src + 3 bytes)
#define CRC_SIZE       2           ///< Size of CRC field in bytes
#define MAX_PAYLOAD    (BUFFER_SIZE - HEADER_SIZE - CRC_SIZE)  ///< Maximum payload size

/**
 * @class LoRaBasicLink
 * @brief Basic implementation of LoRa link layer protocol
 * 
 * This class provides a straightforward implementation of a LoRa link layer
 * with the following features:
 * 
 * - **Packet Structure**: Fixed header format with destination ID, source ID,
 *   sequence number, flags, and payload length
 * - **CRC Protection**: 16-bit CRC for error detection
 * - **Acknowledgments**: Optional ACK mechanism for reliable delivery
 * - **Sequence Numbers**: Automatic sequence numbering for duplicate detection
 * - **Broadcast Support**: Special handling for broadcast messages (no ACK)
 * 
 * The protocol is designed for simple peer-to-peer communication without
 * advanced features like collision avoidance or mesh routing.
 * 
 * @par Usage Example:
 * @code
 * // For real hardware, use: SemtechRadio radio(915000000); (see esp32_platformio example)
 * MockRadio radio;  // For testing/simulation
 * LoRaBasicLink link(&radio, 1, millis, delay);
 * 
 * // Send a message with acknowledgment
 * uint8_t data[] = "Hello World";
 * bool success = link.sendPacket(2, data, 11, true);
 * 
 * // Receive messages
 * uint8_t srcId, buffer[100];
 * int len = link.receivePacket(&srcId, buffer, 100);
 * @endcode
 */
class LoRaBasicLink : public ILoRaLink {
public:
    /**
     * @typedef time_ms_fn
     * @brief Function pointer type for getting current time in milliseconds
     */
    using time_ms_fn = uint32_t (*)();
    
    /**
     * @typedef sleep_ms_fn
     * @brief Function pointer type for sleeping/delaying execution
     */
    using sleep_ms_fn = void (*)(uint32_t);

    // --- Protocol Flag Constants ---
    static constexpr uint8_t FLAG_ACK = 0x01;         ///< Flag indicating this packet is an acknowledgment
    static constexpr uint8_t FLAG_ACK_REQUEST = 0x02; ///< Flag requesting acknowledgment from receiver

    /**
     * @brief Constructor for LoRaBasicLink
     * @param radio Pointer to radio interface implementation
     * @param getTime Function to get current time in milliseconds
     * @param sleep Function to sleep/delay for specified milliseconds
     * 
     * Initializes the basic link with the specified radio interface.
     * The radio must be properly initialized before use. Node IDs are now
     * passed per-packet in sendPacket() calls rather than stored in the link.
     */
    LoRaBasicLink(IRadio* radio, time_ms_fn getTime, sleep_ms_fn sleep);

    /**
     * @brief Send a packet to destination node
     * @param srcId Source node ID (16-bit identifier for this node)
     * @param destId Destination node ID (16-bit identifier, 0xFFFF for broadcast)
     * @param payload Data to transmit
     * @param len Length of payload (must be <= MAX_PAYLOAD)
     * @param requestAck Whether to request acknowledgment (ignored for broadcasts)
     * @param maxRetries Maximum retry attempts if no ACK received (default: 3)
     * @return true if packet sent successfully (and ACK received if requested)
     * 
     * Transmits a packet using the basic protocol format. If acknowledgment is
     * requested, waits up to 500ms for an ACK response and retries transmission
     * if no ACK is received within the timeout period.
     * 
     * @note Broadcast packets (destId = 0xFFFF) do not use acknowledgments
     * @note Maximum payload size is MAX_PAYLOAD bytes
     */
    bool sendPacket(uint16_t srcId, uint16_t destId, const uint8_t* payload, uint8_t len, bool requestAck = false, int maxRetries = 3) override;

    /**
     * @brief Receive a packet from the radio
     * @param srcId Pointer to store source node ID (16-bit) of received packet
     * @param buffer Buffer to store received payload
     * @param maxLen Maximum buffer size
     * @param timeoutMs Maximum time to wait for a packet in milliseconds (default: 1000ms)
     * @return Length of received payload, 0 if no valid packet available
     * 
     * Checks for incoming packets and processes them according to the protocol:
     * - Validates packet format and CRC
     * - Filters packets not addressed to this node (unless broadcast)
     * - Automatically sends ACK if requested by sender
     * - Returns payload data and source ID for valid packets
     * - Waits up to timeoutMs for a packet to arrive
     * 
     * This method may block for up to timeoutMs milliseconds waiting for a packet.
     * The timeout parameter is passed through to the underlying radio layer.
     */
    int receivePacket(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen, uint32_t timeoutMs = 1000) override;

    /**
     * @brief Set the local node ID for address filtering
     * @param localId The 16-bit node ID for this node (used for filtering incoming packets)
     * 
     * This method must be called before receivePacket() to enable proper address filtering.
     * Packets addressed to this ID or to the broadcast address (0xFFFF) will be accepted.
     */
    void setLocalId(uint16_t localId) override;

private:
    IRadio* _radio;          ///< Pointer to radio interface
    uint16_t _localId;       ///< This node's ID (for address filtering)
    uint8_t _seqNum;         ///< Current sequence number for outgoing packets
    time_ms_fn _getTimeMs;   ///< Function to get current time
    sleep_ms_fn _sleepMs;    ///< Function to sleep/delay

    /**
     * @brief Send an acknowledgment packet
     * @param srcId Source node ID to use for the ACK
     * @param to Destination node ID to send ACK to
     * @param seq Sequence number to acknowledge
     * 
     * Sends a minimal ACK packet in response to a received packet that
     * requested acknowledgment. The ACK contains the original sequence
     * number to allow the sender to match it with the original transmission.
     */
    void sendAck(uint16_t srcId, uint16_t to, uint8_t seq);

    /**
     * @brief Calculate 16-bit CRC for data integrity
     * @param data Pointer to data to calculate CRC for
     * @param len Number of bytes to include in CRC calculation
     * @return 16-bit CRC value
     * 
     * Calculates a CRC-16 checksum using a polynomial suitable for
     * error detection in radio communications. Used to validate
     * packet integrity on reception.
     */
    static uint16_t crc16(const uint8_t* data, size_t len);
};

#endif // LORA_BASIC_LINK_H
