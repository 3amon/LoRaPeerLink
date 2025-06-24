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
#define BROADCAST_ADDR 0xFF        ///< Address for broadcast messages
#define BUFFER_SIZE    256         ///< Maximum packet size including headers
#define HEADER_SIZE    5           ///< Size of packet header in bytes
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
 * SemtechRadio radio(915000000);
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
     * @param localId Local node ID (1-254, avoid 0 and 255)
     * @param getTime Function to get current time in milliseconds
     * @param sleep Function to sleep/delay for specified milliseconds
     * 
     * Initializes the basic link with the specified radio and node configuration.
     * The radio must be properly initialized before use. Node IDs should be unique
     * within the network, with 0xFF reserved for broadcast and 0x00 typically avoided.
     */
    LoRaBasicLink(IRadio* radio, uint8_t localId, time_ms_fn getTime, sleep_ms_fn sleep);

    /**
     * @brief Send a packet to destination node
     * @param destId Destination node ID (1-254 for unicast, 0xFF for broadcast)
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
     * @note Broadcast packets (destId = 0xFF) do not use acknowledgments
     * @note Maximum payload size is MAX_PAYLOAD bytes
     */
    bool sendPacket(uint8_t destId, const uint8_t* payload, uint8_t len, bool requestAck = false, int maxRetries = 3) override;

    /**
     * @brief Receive a packet from the radio
     * @param srcId Pointer to store source node ID of received packet
     * @param buffer Buffer to store received payload
     * @param maxLen Maximum buffer size
     * @return Length of received payload, 0 if no valid packet available
     * 
     * Checks for incoming packets and processes them according to the protocol:
     * - Validates packet format and CRC
     * - Filters packets not addressed to this node (unless broadcast)
     * - Automatically sends ACK if requested by sender
     * - Returns payload data and source ID for valid packets
     * 
     * This method is non-blocking and should be called regularly to process
     * incoming messages.
     */
    int receivePacket(uint8_t* srcId, uint8_t* buffer, uint8_t maxLen) override;

private:
    IRadio* _radio;          ///< Pointer to radio interface
    uint8_t _localId;        ///< This node's ID
    uint8_t _seqNum;         ///< Current sequence number for outgoing packets
    time_ms_fn _getTimeMs;   ///< Function to get current time
    sleep_ms_fn _sleepMs;    ///< Function to sleep/delay

    /**
     * @brief Send an acknowledgment packet
     * @param to Destination node ID to send ACK to
     * @param seq Sequence number to acknowledge
     * 
     * Sends a minimal ACK packet in response to a received packet that
     * requested acknowledgment. The ACK contains the original sequence
     * number to allow the sender to match it with the original transmission.
     */
    void sendAck(uint8_t to, uint8_t seq);

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
