/**
 * @file SemtechRadio.cpp
 * @brief Implementation of Semtech LoRa radio hardware abstraction layer
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements the SemtechRadio class, providing hardware-specific
 * functionality for Semtech LoRa radio chipsets. It handles radio initialization,
 * event management, and provides a simplified interface for LoRa communication
 * without LoRaWAN protocol overhead.
 */

#include "SemtechRadio.h"
#include <string.h>

/**
 * @brief Global instance pointer for callback forwarding
 * 
 * The Semtech library uses C-style callbacks, so we need a global instance
 * to forward events to the appropriate object methods. Only one SemtechRadio
 * instance should be active at a time.
 */
SemtechRadio* SemtechRadio::_globalInstance = nullptr;

/**
 * @brief Constructor initializes radio with frequency and default settings
 * 
 * Creates a new SemtechRadio instance and sets it as the global instance
 * for callback forwarding. Initializes internal state variables and
 * configures the radio for the specified frequency.
 */
SemtechRadio::SemtechRadio(uint32_t frequency)
    : _frequency(frequency),
      _rxDoneFlag(false),
      _txDoneFlag(false),
      _lastRssi(0),
      _lastSnr(0),
      _rxLen(0) {
    _globalInstance = this; // Set global instance for callback forwarding
}

bool SemtechRadio::begin() {
    memset(&_events, 0, sizeof(_events));
    _events.TxDone = &SemtechRadio::onTxDoneStatic;
    _events.TxTimeout = &SemtechRadio::onTxTimeoutStatic;
    _events.RxDone = &SemtechRadio::onRxDoneStatic;
    _events.RxTimeout = &SemtechRadio::onRxTimeoutStatic;
    _events.RxError = &SemtechRadio::onRxErrorStatic;

    Mcu.begin();

    Radio.Init(&_events);
    Radio.SetChannel(_frequency);
    Radio.SetModem(MODEM_LORA);

    Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
        LORA_SPREADING_FACTOR, LORA_CODINGRATE,
        LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
        true, 0, 0, LORA_IQ_INVERSION_ON, 3000 );

    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                0, true, 0, 0, LORA_IQ_INVERSION_ON, true );
    return true;
}

bool SemtechRadio::send(const uint8_t* data, size_t length) {
    _txDoneFlag = false;
    Radio.Send((uint8_t*)data, (uint8_t)length);

    uint32_t start = _get_time_ms();
    while (!_txDoneFlag && (_get_time_ms() - start < 1000)) {
        Radio.IrqProcess();
    }
    return _txDoneFlag;
}

int SemtechRadio::receive(uint8_t* buffer, size_t maxLen, unsigned long timeoutMs) {
    _rxDoneFlag = false;
    _rxLen = 0;
    Radio.Rx(timeoutMs);

    uint32_t start = _get_time_ms();
    while (!_rxDoneFlag && (_get_time_ms() - start < timeoutMs)) {
        Radio.IrqProcess();
    }

    if (_rxDoneFlag && _rxLen > 0 && _rxLen <= maxLen) {
        memcpy(buffer, _rxBuffer, _rxLen);
        return _rxLen;
    }
    return 0;
}

int SemtechRadio::packetRssi() {
    return _lastRssi;
}

float SemtechRadio::packetSnr() {
    return _lastSnr;
}

uint32_t SemtechRadio::_get_time_ms() {
    // Replace this with something like esp_timer_get_time() / 1000
    extern uint32_t get_time_ms();
    return get_time_ms();
}

void SemtechRadio::onTxDoneStatic() {
    if (_globalInstance) _globalInstance->_txDoneFlag = true;
}

void SemtechRadio::onTxTimeoutStatic() {
    // Empty implementation
}

void SemtechRadio::onRxDoneStatic(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
    if (_globalInstance) {
        _globalInstance->_lastRssi = rssi;
        _globalInstance->_lastSnr = snr;
        _globalInstance->_rxLen = size > BUFFER_SIZE ? BUFFER_SIZE : size;
        memcpy(_globalInstance->_rxBuffer, payload, _globalInstance->_rxLen);
        _globalInstance->_rxDoneFlag = true;
    }
}

void SemtechRadio::onRxTimeoutStatic() {
    // Empty implementation
}

void SemtechRadio::onRxErrorStatic() {
    // Empty implementation
}