/**
 * @file LoraBackoffLink.cpp
 * @brief LoRa link layer with random access delay and exponential backoff
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * All protocol logic lives in LoRaLinkCore; this file only supplies timing.
 */

#include "LoraBackoffLink.h"

constexpr uint32_t LoRaBackoffLink::DEFAULT_ACK_TIMEOUT_MS;

LoRaBackoffLink::LoRaBackoffLink(IRadio* radio, time_ms_fn getTime, sleep_ms_fn sleep)
    : LoRaLinkCore(radio, getTime, sleep, DEFAULT_ACK_TIMEOUT_MS) {}

uint32_t LoRaBackoffLink::preSendDelayMs(int attempt, uint32_t slot) {
    (void)attempt;
    return slot / 10 + randomBelow(slot * 4 / 10);
}
