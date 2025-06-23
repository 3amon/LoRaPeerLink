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
    SemtechRadio(uint32_t frequency = RF_FREQUENCY);

    bool begin() override;

    bool send(const uint8_t* data, size_t length) override;

    int receive(uint8_t* buffer, size_t maxLen, unsigned long timeoutMs = RX_TIMEOUT_VALUE) override;

    int packetRssi() override;
    float packetSnr() override;

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
    uint32_t _get_time_ms();

    // --- Static forwarding (trampoline) ---
    static SemtechRadio* _globalInstance;  // for now: one radio active at a time

    static void onTxDoneStatic();
    static void onTxTimeoutStatic();
    static void onRxDoneStatic(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr);
    static void onRxTimeoutStatic();
    static void onRxErrorStatic();
};

IRadio* GetRadio();  // Singleton-style getter

#endif // SEMTECH_RADIO_H
