/**
 * @file ILoRaLink.h
 * @brief Abstract interface for LoRa link layer implementations
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file defines the abstract interface for LoRa link layer protocols.
 * It provides a common interface that allows different link implementations
 * to be used interchangeably, supporting various strategies for packet
 * transmission, acknowledgment, and collision avoidance.
 */

#ifndef ILORA_LINK_H
#define ILORA_LINK_H

#include <stdint.h>

/**
 * @class ILoRaLink
 * @brief Abstract interface for LoRa link layer implementations
 * 
 * This pure virtual class defines the interface that all LoRa link layer
 * implementations must provide. It abstracts different transmission strategies
 * and protocols, allowing applications to work with various link types:
 * 
 * - Basic links (simple send/receive)
 * - Backoff links (collision avoidance)
 * - Future implementations (routing, mesh, etc.)
 * 
 * The interface supports both acknowledged and unacknowledged transmission,
 * with configurable retry mechanisms for reliable communication.
 */
class ILoRaLink {
public:
    /**
     * @typedef time_ms_fn
     * @brief Function pointer type for getting current time in milliseconds
     * 
     * Used by link implementations to track timing for retransmissions,
     * acknowledgment timeouts, and backoff calculations.
     */
    using time_ms_fn = uint32_t (*)();
    
    /**
     * @typedef sleep_ms_fn  
     * @brief Function pointer type for sleeping/delaying execution
     * @param ms Number of milliseconds to sleep
     * 
     * Used by link implementations for timing control, backoff delays,
     * and power management during idle periods.
     */
    using sleep_ms_fn = void (*)(uint32_t);

    /**
     * @brief Virtual destructor for proper cleanup of derived classes
     */
    virtual ~ILoRaLink() = default;

    /**
     * @brief Send a packet to a destination node
     * @param srcId Source node ID (16-bit identifier for this node)
     * @param destId Destination node ID (16-bit identifier), or 0xFFFF for broadcast
     * @param payload Pointer to data to transmit
     * @param len Length of payload data in bytes
     * @param requestAck Whether to request acknowledgment from receiver (default: false)
     * @param maxRetries Maximum number of retry attempts if ACK requested (default: 3)
     * @return true if packet was sent successfully (and ACK received if requested), false otherwise
     * 
     * Transmits a packet to the specified destination. If acknowledgment is requested,
     * the method will wait for an ACK response and retry transmission up to maxRetries
     * times if no ACK is received. Broadcast packets (destId = 0xFFFF) typically do not
     * use acknowledgments.
     * 
     * The exact behavior depends on the implementation:
     * - Basic links: Simple transmission with optional ACK
     * - Backoff links: Collision avoidance with exponential backoff
     */
    virtual bool sendPacket(uint16_t srcId, uint16_t destId, const uint8_t* payload, uint8_t len, bool requestAck = false, int maxRetries = 3) = 0;

    /**
     * @brief Receive a packet from any source
     * @param srcId Pointer to variable that will store the source node ID (16-bit)
     * @param buffer Buffer to store the received payload data
     * @param maxLen Maximum number of bytes that can be stored in buffer
     * @return Length of received payload data, 0 if no packet available or error occurred
     * 
     * Attempts to receive a packet from the radio. This method typically performs
     * a non-blocking check for available data. If a valid packet is received that
     * is addressed to this node (or is a broadcast), the payload is copied to the
     * buffer and the source ID is returned via the srcId parameter.
     * 
     * The method handles:
     * - Packet validation (CRC, format checking)
     * - Address filtering (drops packets not for this node)
     * - Automatic ACK transmission (if requested by sender)
     * - Protocol-specific packet processing
     */
    virtual int receivePacket(uint16_t* srcId, uint8_t* buffer, uint8_t maxLen) = 0;

    /**
     * @brief Set the local node ID for address filtering
     * @param localId The 16-bit node ID for this node (used for filtering incoming packets)
     * 
     * This method must be called before receivePacket() to enable proper address filtering.
     * Packets addressed to this ID or to the broadcast address (0xFFFF) will be accepted.
     */
    virtual void setLocalId(uint16_t localId) = 0;
};

#endif // ILORA_LINK_H