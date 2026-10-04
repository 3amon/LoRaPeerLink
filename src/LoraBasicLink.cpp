/**
 * @file LoraBasicLink.cpp
 * @brief Basic LoRa link layer
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * All protocol logic lives in LoRaLinkCore. The basic link uses its default
 * timing: no delay before a transmission and a short random pause between
 * retries.
 */

#include "LoraBasicLink.h"

constexpr uint32_t LoRaBasicLink::DEFAULT_ACK_TIMEOUT_MS;

LoRaBasicLink::LoRaBasicLink(IRadio* radio, time_ms_fn getTime, sleep_ms_fn sleep)
    : LoRaLinkCore(radio, getTime, sleep, DEFAULT_ACK_TIMEOUT_MS) {}
