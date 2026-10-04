/**
 * @file LoraBasicLink.h
 * @brief Basic LoRa link layer: framing, CRC, optional acknowledgment
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * LoRaBasicLink transmits immediately and, when an acknowledgment is
 * requested, retries a few times with a short random pause. Use
 * LoRaBackoffLink on busier channels.
 *
 * The frame format, ACK handling, duplicate suppression and receive queue
 * are implemented in LoRaLinkCore and shared with LoRaBackoffLink, so the
 * two link types can talk to each other.
 */

#ifndef LORA_BASIC_LINK_H
#define LORA_BASIC_LINK_H

#include "LoRaLinkCore.h"

/**
 * @class LoRaBasicLink
 * @brief Straightforward point-to-point and broadcast link
 *
 * Example:
 * @code
 * LoRaBasicLink link(&radio, getTimeMs, sleepMs);
 * link.setLocalId(0x0001);
 *
 * // Unacknowledged broadcast
 * link.sendPacket(0x0001, BROADCAST_ADDR, data, len);
 *
 * // Acknowledged unicast, up to 3 transmissions
 * bool delivered = link.sendPacket(0x0001, 0x0002, data, len, true, 3);
 *
 * // Receive (blocks for up to one second)
 * uint16_t from;
 * uint8_t buffer[MAX_PAYLOAD];
 * int n = link.receivePacket(&from, buffer, MAX_PAYLOAD, 1000);
 * @endcode
 */
class LoRaBasicLink : public LoRaLinkCore {
public:
    /** Default time to wait for an ACK after each transmission. */
    static constexpr uint32_t DEFAULT_ACK_TIMEOUT_MS = 500;

    /**
     * @param radio   Radio driver (must outlive the link)
     * @param getTime Function returning a millisecond clock
     * @param sleep   Function that blocks for the given number of milliseconds
     */
    LoRaBasicLink(IRadio* radio, time_ms_fn getTime, sleep_ms_fn sleep);
};

#endif // LORA_BASIC_LINK_H
