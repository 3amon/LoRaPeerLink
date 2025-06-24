/**
 * @file LoraBackoffLink.h
 * @brief Advanced LoRa link implementation with collision avoidance and exponential backoff
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements an advanced LoRa link layer protocol that provides:
 * - Exponential backoff for collision avoidance
 * - Intelligent retry mechanisms with adaptive timing
 * - Enhanced reliability for high-traffic scenarios
 * - Compatible packet format with LoRaBasicLink
 * 
 * The protocol uses the same packet structure as LoRaBasicLink but adds
 * sophisticated timing and retry logic to handle network congestion and
 * reduce packet collisions in multi-node scenarios.
 */

#ifndef LORA_BACKOFF_LINK_H
#define LORA_BACKOFF_LINK_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "IRadio.h"
#include "ILoRaLink.h"

#define BROADCAST_ADDR 0xFFFF ///< Address for broadcast messages (16-bit)
#define BUFFER_SIZE    256    ///< Maximum packet size including headers

/**
 * @class LoRaBackoffLink
 * @brief Advanced LoRa link implementation with collision avoidance
 * 
 * This class extends the basic LoRa link protocol with sophisticated
 * collision avoidance mechanisms designed for networks with multiple
 * active nodes. Key features include:
 * 
 * - **Exponential Backoff**: Progressively longer delays between retries
 *   to reduce collision probability in congested networks
 * - **Adaptive Timing**: Smart retry intervals based on network conditions
 * - **Enhanced Reliability**: Improved packet delivery in high-traffic scenarios
 * - **Protocol Compatibility**: Uses same packet format as LoRaBasicLink
 * 
 * The backoff algorithm works by:
 * 1. Initial transmission attempt
 * 2. If collision detected or no ACK received, wait for random backoff period
 * 3. Exponentially increase backoff window on each retry
 * 4. Randomize exact delay within backoff window to avoid synchronized retries
 * 
 * This approach significantly improves network throughput and reduces
 * packet loss in scenarios with multiple simultaneously transmitting nodes.
 * 
 * @par Usage Example:
 * @code
 * SemtechRadio radio(915000000);
 * LoRaBackoffLink link(&radio, 1, millis, delay);
 * 
 * // Send with automatic backoff and retry
 * uint8_t data[] = "Critical Data";
 * bool success = link.sendPacket(2, data, 13, true, 5); // 5 retries
 * @endcode
 */
class LoRaBackoffLink : public ILoRaLink {
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

    /**
     * @brief Constructor for LoRaBackoffLink
     * @param radio Pointer to radio interface implementation
     * @param getTime Function to get current time in milliseconds
     * @param sleep Function to sleep/delay for specified milliseconds
     * 
     * Initializes the backoff link with the specified radio interface.
     * The radio must be properly initialized before use. Node IDs are now
     * passed per-packet in sendPacket() calls rather than stored in the link.
     */
    LoRaBackoffLink(IRadio* radio, time_ms_fn getTime, sleep_ms_fn sleep);

    /**
     * @brief Send a packet with exponential backoff retry logic
     * @param srcId Source node ID (16-bit identifier for this node)
     * @param dest Destination node ID (16-bit identifier, 0xFFFF for broadcast)
     * @param data Payload data to transmit
     * @param len Length of payload data
     * @param requireAck Whether to require acknowledgment (ignored for broadcasts)
     * @param maxRetries Maximum number of retry attempts with backoff (default: 3)
     * @return true if packet sent successfully (and ACK received if required)
     * 
     * Transmits a packet using exponential backoff algorithm:
     * 1. Attempt initial transmission
     * 2. If no ACK received (when required), wait for backoff period
     * 3. Retry with exponentially increasing backoff window
     * 4. Randomize delay within window to avoid synchronized retries
     * 5. Repeat until success or maxRetries exceeded
     * 
     * The backoff window starts small and doubles with each retry, helping
     * to resolve collision scenarios automatically.
     */
    bool sendPacket(uint16_t srcId, uint16_t dest, const uint8_t* data, uint8_t len, bool requireAck = false, int maxRetries = 3) override;

    /**
     * @brief Receive a packet and handle protocol processing
     * @param src Pointer to store source node ID (16-bit) of received packet
     * @param buffer Buffer to store received payload data
     * @param maxLen Maximum buffer size for payload
     * @return Length of received payload, 0 if no valid packet available
     * 
     * Processes incoming packets with the same protocol handling as LoRaBasicLink:
     * - Validates packet format and CRC
     * - Filters packets not addressed to this node
     * - Automatically sends ACK responses when requested
     * - Returns payload data for application processing
     * 
     * This method is non-blocking and should be called regularly.
     */
    int receivePacket(uint16_t* src, uint8_t* buffer, uint8_t maxLen) override;

    /**
     * @brief Set the local node ID for address filtering
     * @param localId The 16-bit node ID for this node (used for filtering incoming packets)
     * 
     * This method must be called before receivePacket() to enable proper address filtering.
     * Packets addressed to this ID or to the broadcast address (0xFFFF) will be accepted.
     */
    void setLocalId(uint16_t localId) override;

private:
    IRadio* _radio;        ///< Pointer to radio interface
    uint16_t _localId;     ///< This node's ID (for address filtering)
    uint8_t _seqNum;       ///< Current sequence number for outgoing packets
    time_ms_fn _getTime;   ///< Function to get current time
    sleep_ms_fn _sleep;    ///< Function to sleep/delay

    /**
     * @enum Flags
     * @brief Protocol flags used in packet headers
     */
    enum Flags : uint8_t {
        FLAG_ACK        = 0x01,  ///< Packet is an acknowledgment
        FLAG_NEEDS_ACK  = 0x02   ///< Packet requires acknowledgment
    };

    /**
     * @struct PacketHeader
     * @brief Packed structure defining packet header format
     * 
     * This structure defines the exact byte layout of packet headers,
     * ensuring compatibility across different platforms and compilers.
     * The packed attribute prevents compiler padding between fields.
     */
    struct __attribute__((packed)) PacketHeader {
        uint16_t dst;   ///< Destination node ID (16-bit)
        uint16_t src;   ///< Source node ID (16-bit)
        uint8_t seq;    ///< Sequence number
        uint8_t flags;  ///< Protocol flags (ACK, NEED_ACK, etc.)
        uint8_t len;    ///< Length of payload data
    };

    /**
     * @brief Calculate maximum payload size for current configuration
     * @return Maximum number of payload bytes that can be transmitted
     * 
     * Determines the maximum payload size based on buffer size and
     * protocol overhead (header + CRC). This may vary based on
     * radio configuration or protocol options.
     */
    uint8_t maxPayloadSize() const;

    /**
     * @brief Send an acknowledgment packet
     * @param srcId Source node ID to use for the ACK
     * @param to Destination node ID to send ACK to
     * @param seq Sequence number being acknowledged
     * 
     * Sends a minimal ACK packet in response to a received packet
     * that requested acknowledgment. The ACK includes the original
     * sequence number for proper matching.
     */
    void sendAck(uint16_t srcId, uint16_t to, uint8_t seq);

    /**
     * @brief Wait for acknowledgment with timeout
     * @param expectedSeq Expected sequence number in ACK response
     * @param timeoutMs Maximum time to wait for ACK in milliseconds
     * @return true if ACK received within timeout, false otherwise
     * 
     * Waits for an ACK packet matching the expected sequence number.
     * Used during the retry process to determine if retransmission
     * is necessary.
     */
    bool waitForAck(uint8_t expectedSeq, uint32_t timeoutMs);

    /**
     * @brief Calculate 16-bit CRC for packet integrity
     * @param data Pointer to data for CRC calculation
     * @param len Number of bytes to include in CRC
     * @return 16-bit CRC value
     * 
     * Calculates CRC-16 checksum for error detection. Uses the same
     * algorithm as LoRaBasicLink for packet compatibility.
     */
    uint16_t crc16(const uint8_t* data, size_t len);
};

#endif // LORA_BACKOFF_LINK_H
