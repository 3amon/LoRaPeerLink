#ifndef ILORA_LINK_H
#define ILORA_LINK_H

#include <stdint.h>

/**
 * Abstract interface for LoRa link implementations
 * Provides common interface for both basic and backoff link variants
 */
class ILoRaLink {
public:
    using time_ms_fn = uint32_t (*)();
    using sleep_ms_fn = void (*)(uint32_t);

    virtual ~ILoRaLink() = default;

    /**
     * Send a packet to destination
     * @param destId Destination node ID
     * @param payload Data to send
     * @param len Length of payload
     * @param requestAck Whether to request acknowledgment (optional)
     * @param maxRetries Maximum retry attempts (optional, for backoff implementations)
     * @return true if packet was sent successfully
     */
    virtual bool sendPacket(uint8_t destId, const uint8_t* payload, uint8_t len, bool requestAck = false, int maxRetries = 3) = 0;

    /**
     * Receive a packet
     * @param srcId Pointer to store source node ID
     * @param buffer Buffer to store received data
     * @param maxLen Maximum buffer size
     * @return Length of received data, 0 if no packet or error
     */
    virtual int receivePacket(uint8_t* srcId, uint8_t* buffer, uint8_t maxLen) = 0;
};

#endif // ILORA_LINK_H