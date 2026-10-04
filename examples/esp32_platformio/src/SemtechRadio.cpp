/**
 * @file SemtechRadio.cpp
 * @brief IRadio driver for SX126x boards using the Heltec/Semtech radio stack
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * See SemtechRadio.h for how this driver keeps the radio receiving.
 */

#include "SemtechRadio.h"

#include <Arduino.h>

SemtechRadio* SemtechRadio::_instance = nullptr;

SemtechRadio::SemtechRadio(uint32_t frequency, uint8_t spreadingFactor, int8_t txPowerDbm)
    : _frequency(frequency), _spreadingFactor(spreadingFactor), _txPowerDbm(txPowerDbm), _begun(false),
      _receiving(false), _txDone(false), _txFailed(false), _rxReady(false), _rxLen(0),
      _lastRssi(0), _lastSnr(0),
      _framesReceived(0), _framesSent(0), _crcErrors(0), _txTimeouts(0) {
    _instance = this;
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

    Radio.SetTxConfig(MODEM_LORA, _txPowerDbm, 0, LORA_BANDWIDTH,
                      _spreadingFactor, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

    // Last argument: continuous receive.
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, _spreadingFactor,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

    _begun = true;
    _rxReady = false;
    startReceiving();
    return true;
}

void SemtechRadio::startReceiving() {
    // Timeout 0: no software timer, the chip stays in receive until told
    // otherwise. Only ever called when the chip is NOT receiving, because
    // re-issuing the command restarts the receiver and loses a frame that is
    // in flight.
    Radio.Rx(0);
    _receiving = true;
}

bool SemtechRadio::send(const uint8_t* data, size_t length) {
    if (!_begun || data == nullptr || length == 0 || length > 255) {
        return false;
    }

    // Pick up a frame that finished just before we take the radio away.
    Radio.IrqProcess();

    _txDone = false;
    _txFailed = false;
    _receiving = false;
    Radio.Send(const_cast<uint8_t*>(data), static_cast<uint8_t>(length));

    // Wait for the frame to leave: its time on air plus a generous margin.
    const uint32_t limit = Radio.TimeOnAir(MODEM_LORA, static_cast<uint8_t>(length)) + 500;
    const uint32_t start = millis();
    while (!_txDone && !_txFailed && (millis() - start < limit)) {
        Radio.IrqProcess();
        delay(1);   // Let other tasks run; 1 ms resolution is plenty here
    }

    const bool ok = _txDone;
    if (ok) {
        ++_framesSent;
    } else {
        ++_txTimeouts;
        Radio.Standby();
    }

    // Listen again straight away, before the caller gets around to asking.
    startReceiving();
    return ok;
}

int SemtechRadio::receive(uint8_t* buffer, size_t maxLen, unsigned long timeoutMs) {
    if (!_begun || buffer == nullptr) {
        return 0;
    }
    if (!_receiving) {
        startReceiving();
    }

    const uint32_t start = millis();
    for (;;) {
        Radio.IrqProcess();
        if (_rxReady) break;
        if (!_receiving) startReceiving();              // The chip dropped out of receive
        if (millis() - start >= timeoutMs) break;
        delay(1);
    }

    if (!_rxReady) {
        return 0;
    }

    const size_t len = _rxLen;
    _rxReady = false;
    if (len == 0 || len > maxLen) {
        return 0;
    }
    memcpy(buffer, _rxBuffer, len);
    return static_cast<int>(len);
}

int SemtechRadio::packetRssi() {
    return _lastRssi;
}

float SemtechRadio::packetSnr() {
    return _lastSnr;
}

uint32_t SemtechRadio::timeOnAirMs(size_t length) {
    if (!_begun || length > 255) return 0;
    return Radio.TimeOnAir(MODEM_LORA, static_cast<uint8_t>(length));
}

// --- Radio event callbacks (called from Radio.IrqProcess()) ---

void SemtechRadio::onTxDoneStatic() {
    if (_instance) _instance->_txDone = true;
}

void SemtechRadio::onTxTimeoutStatic() {
    if (_instance) _instance->_txFailed = true;
}

void SemtechRadio::onRxDoneStatic(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
    SemtechRadio* self = _instance;
    if (!self) return;
    const size_t len = size > sizeof(self->_rxBuffer) ? sizeof(self->_rxBuffer) : size;
    memcpy(self->_rxBuffer, payload, len);
    self->_rxLen = len;
    self->_lastRssi = rssi;
    self->_lastSnr = snr;
    self->_rxReady = true;      // Overwrites an unread frame: newest wins
    ++self->_framesReceived;
    // In continuous mode the chip keeps receiving; nothing to restart.
}

void SemtechRadio::onRxTimeoutStatic() {
    // Not expected in continuous mode (the vendor stack also reports header
    // errors through this callback). Make sure we are receiving again.
    if (_instance) _instance->_receiving = false;
}

void SemtechRadio::onRxErrorStatic() {
    // CRC error: the chip stays in continuous receive.
    if (_instance) ++_instance->_crcErrors;
}
