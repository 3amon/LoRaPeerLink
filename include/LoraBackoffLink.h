/**
 * @file LoraBackoffLink.h
 * @brief LoRa link layer with random access delay and exponential backoff
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * LoRaBackoffLink is meant for channels shared by several nodes. On top of
 * the exponential retry backoff that every link has, it waits a short random
 * time before every transmission, including the first one. That keeps nodes
 * that react to the same event (for example a broadcast query) from all
 * transmitting at the same instant.
 *
 * The link keeps listening while it waits, and frames that arrive in the
 * meantime are queued for receivePacket(). The frame format is identical to
 * LoRaBasicLink (see LoRaLinkCore.h), so both link types interoperate.
 */

#ifndef LORA_BACKOFF_LINK_H
#define LORA_BACKOFF_LINK_H

#include "LoRaLinkCore.h"

class LoRaBackoffLink : public LoRaLinkCore {
public:
    /** Default time to wait for an ACK after each transmission. */
    static constexpr uint32_t DEFAULT_ACK_TIMEOUT_MS = 300;

    /**
     * @param radio   Radio driver (must outlive the link)
     * @param getTime Function returning a millisecond clock
     * @param sleep   Function that blocks for the given number of milliseconds
     */
    LoRaBackoffLink(IRadio* radio, time_ms_fn getTime, sleep_ms_fn sleep);

protected:
    /** Random access delay: between 10% and 50% of a slot (10-50 ms at SF7). */
    uint32_t preSendDelayMs(int attempt, uint32_t slotMs) override;
};

#endif // LORA_BACKOFF_LINK_H
