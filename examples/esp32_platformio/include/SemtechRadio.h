/**
 * @file SemtechRadio.h
 * @brief IRadio driver for SX126x boards using the Heltec/Semtech radio stack
 * @author LoRaPeerLink Project
 * @version 2.0
 *
 * Written for the Heltec WiFi LoRa 32 V3 / Wireless Stick V3 (ESP32-S3 +
 * SX1262) with the vendor "LoRaWan_APP" radio layer that ships in lib/.
 *
 * How this driver receives
 * ------------------------
 * The radio is kept in continuous receive whenever it is not transmitting:
 *
 *   - begin() and send() both leave the radio receiving.
 *   - receive() does not restart the receiver. It only waits for a frame, so
 *     a frame that is already on the air when the timeout expires is not
 *     aborted; it is returned by the next receive() call.
 *   - A frame that completes between two receive() calls is kept by the
 *     radio and returned by the next call.
 *
 * Version 1 of this driver called Radio.Rx() at the start of every receive()
 * call. That restarts the receiver and throws away whatever frame was being
 * received at that moment, and it left the radio idle after every
 * transmission until the next receive() call. Both cost packets.
 *
 * The SX126x holds one received frame at a time. If two frames arrive while
 * nobody calls receive() (or send()), the first one is lost, so call
 * receive() (through the link / RollCall / PeerMessenger processMessages
 * functions) regularly and with generous timeouts.
 *
 * Hardware status: run on two Heltec Wireless Shell V3 modules (ESP32-S3 +
 * SX1262) at SF7 / 125 kHz / 915 MHz with examples/esp32_bench_test. Other
 * boards and settings have not been tried.
 *
 * Vendor license
 * --------------
 * The vendor stack (libheltec.a) only starts on a board that has a Heltec
 * license stored in flash. Without one, Mcu.begin() prints
 * "ESP32ChipID=XXXXXXXXXXXX" and then waits forever for a license on UART0.
 * On a board whose console is the native USB port nothing more appears, so
 * begin() simply never returns. Heltec development boards normally ship
 * licensed; bare modules, and boards whose flash has been fully erased or
 * re-partitioned, may not be. See examples/esp32_bench_test/README.md for
 * how to look the license up and store it.
 *
 * Build with -D SEMTECH_RADIO_TRACE to print each bring-up step.
 */

#ifndef SEMTECH_RADIO_H
#define SEMTECH_RADIO_H

#include "IRadio.h"
#include "LoRaWan_APP.h"
#include <stdint.h>
#include <string.h>

// --- Default radio configuration (every node on a network must match) ---
#define RF_FREQUENCY                  915000000  ///< Hz (915 MHz ISM band)
#define TX_OUTPUT_POWER               14         ///< dBm
#define LORA_BANDWIDTH                0          ///< 0 = 125 kHz, 1 = 250 kHz, 2 = 500 kHz
#define LORA_SPREADING_FACTOR         7          ///< SF7..SF12
#define LORA_CODINGRATE               1          ///< 1 = 4/5 ... 4 = 4/8
#define LORA_PREAMBLE_LENGTH          12         ///< Symbols
#define LORA_SYMBOL_TIMEOUT           0
#define LORA_FIX_LENGTH_PAYLOAD_ON    false
#define LORA_IQ_INVERSION_ON          false
#define RX_TIMEOUT_VALUE              1000       ///< Default receive() timeout in ms

class SemtechRadio : public IRadio {
public:
    /**
     * @param frequency       Carrier frequency in Hz
     * @param spreadingFactor LoRa spreading factor, 7..12
     * @param txPowerDbm      Transmit power in dBm
     */
    SemtechRadio(uint32_t frequency = RF_FREQUENCY,
                 uint8_t spreadingFactor = LORA_SPREADING_FACTOR,
                 int8_t txPowerDbm = TX_OUTPUT_POWER);

    /** Initialise the radio and start receiving. Call once from setup(). */
    bool begin() override;

    /**
     * Transmit one frame (1..255 bytes) and wait until it has left the
     * antenna. The radio is put back into receive before this returns.
     */
    bool send(const uint8_t* data, size_t length) override;

    /**
     * Wait up to timeoutMs for a frame. Returns immediately if one arrived
     * since the last call. Returns 0 on timeout or if the frame does not fit
     * in the buffer.
     */
    int receive(uint8_t* buffer, size_t maxLen, unsigned long timeoutMs = RX_TIMEOUT_VALUE) override;

    int packetRssi() override;
    float packetSnr() override;

    /** Time on air for a frame of the given length with the current settings. */
    uint32_t timeOnAirMs(size_t length) override;

    /**
     * Signal level on the channel right now, in dBm (the radio must be
     * receiving). A quiet 915 MHz channel reads around -100 dBm or lower; a
     * nearby transmitter raises it well above that. Useful for telling an RF
     * problem (antenna, power) from a protocol problem.
     */
    int channelRssi();

    // Diagnostics
    uint32_t framesReceived() const { return _framesReceived; }
    uint32_t framesSent() const { return _framesSent; }
    uint32_t crcErrors() const { return _crcErrors; }
    uint32_t txTimeouts() const { return _txTimeouts; }

private:
    void startReceiving();

    uint32_t _frequency;
    uint8_t _spreadingFactor;
    int8_t _txPowerDbm;
    bool _begun;

    // Shared with the radio event callbacks. The callbacks run from
    // Radio.IrqProcess(), which this class calls from the application
    // thread, but they are volatile so the compiler never caches them across
    // those calls.
    volatile bool _receiving;      ///< The chip is in (continuous) receive
    volatile bool _txDone;
    volatile bool _txFailed;
    volatile bool _rxReady;        ///< _rxBuffer holds an unread frame
    volatile size_t _rxLen;
    volatile int16_t _lastRssi;
    volatile int8_t _lastSnr;
    uint8_t _rxBuffer[255];

    uint32_t _framesReceived;
    uint32_t _framesSent;
    uint32_t _crcErrors;
    uint32_t _txTimeouts;

    RadioEvents_t _events;

    // The vendor stack takes plain C callbacks, so they are forwarded to the
    // (single) instance.
    static SemtechRadio* _instance;
    static void onTxDoneStatic();
    static void onTxTimeoutStatic();
    static void onRxDoneStatic(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr);
    static void onRxTimeoutStatic();
    static void onRxErrorStatic();
};

#endif // SEMTECH_RADIO_H
