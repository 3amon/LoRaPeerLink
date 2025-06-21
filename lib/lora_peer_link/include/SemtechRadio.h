#ifndef SEMTECH_RADIO_H
#define SEMTECH_RADIO_H

#include "IRadio.h"
#include "LoRaWan_APP.h"
#include <stdint.h>
#include <string.h>

// --- Config ---
#define RF_FREQUENCY                  915000000
#define TX_OUTPUT_POWER               14
#define LORA_BANDWIDTH                0
#define LORA_SPREADING_FACTOR         7
#define LORA_CODINGRATE               1
#define LORA_PREAMBLE_LENGTH          12
#define LORA_SYMBOL_TIMEOUT           0
#define LORA_FIX_LENGTH_PAYLOAD_ON    false
#define LORA_IQ_INVERSION_ON          false
#define RX_TIMEOUT_VALUE              1000

class SemtechRadio : public IRadio {
public:
    SemtechRadio(uint32_t frequency = RF_FREQUENCY)
        : _frequency(frequency),
          _rxDoneFlag(false),
          _txDoneFlag(false),
          _lastRssi(0),
          _lastSnr(0),
          _rxLen(0) {
        _globalInstance = this;
    }

    bool begin() override {
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

    bool send(const uint8_t* data, size_t length) override {
        _txDoneFlag = false;
        Radio.Send((uint8_t*)data, (uint8_t)length);

        uint32_t start = _get_time_ms();
        while (!_txDoneFlag && (_get_time_ms() - start < 1000)) {
            Radio.IrqProcess();
        }
        return _txDoneFlag;
    }

    int receive(uint8_t* buffer, size_t maxLen, unsigned long timeoutMs = RX_TIMEOUT_VALUE) override {
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

    int packetRssi() override { return _lastRssi; }
    float packetSnr() override { return _lastSnr; }

private:
    uint32_t _frequency;
    bool _rxDoneFlag;
    bool _txDoneFlag;
    int16_t _lastRssi;
    int8_t _lastSnr;
    size_t _rxLen;
    uint8_t _rxBuffer[BUFFER_SIZE];
    RadioEvents_t _events;

    // Platform-specific time function stub (replace with your own)
    uint32_t _get_time_ms() {
        // Replace this with something like esp_timer_get_time() / 1000
        extern uint32_t get_time_ms();
        return get_time_ms();
    }

    // --- Static forwarding (trampoline) ---
    static SemtechRadio* _globalInstance;  // for now: one radio active at a time

    static void onTxDoneStatic() { if (_globalInstance) _globalInstance->_txDoneFlag = true; }
    static void onTxTimeoutStatic() {}
    static void onRxDoneStatic(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
        if (_globalInstance) {
            _globalInstance->_lastRssi = rssi;
            _globalInstance->_lastSnr = snr;
            _globalInstance->_rxLen = size > BUFFER_SIZE ? BUFFER_SIZE : size;
            memcpy(_globalInstance->_rxBuffer, payload, _globalInstance->_rxLen);
            _globalInstance->_rxDoneFlag = true;
        }
    }
    static void onRxTimeoutStatic() {}
    static void onRxErrorStatic() {}
};

IRadio* GetRadio();  // Singleton-style getter

#endif // SEMTECH_RADIO_H
