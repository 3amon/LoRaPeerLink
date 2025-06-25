/**
 * @file SemtechRadio.h
 * @brief Hardware abstraction layer for Semtech LoRa radio modules
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements the IRadio interface for Semtech LoRa radio chipsets,
 * providing hardware-specific functionality for:
 * - LoRa radio initialization and configuration
 * - Transmission and reception of packets
 * - RSSI and SNR measurement
 * - Event-driven callback handling
 * 
 * The implementation uses the Semtech LoRaWAN library for low-level radio
 * operations while providing a simplified interface for peer-to-peer
 * communication without LoRaWAN overhead.
 */

#ifndef SEMTECH_RADIO_H
#define SEMTECH_RADIO_H

#include "IRadio.h"
#include "LoRaWan_APP.h"
#include <stdint.h>
#include <string.h>

// --- Default Radio Configuration ---
#define RF_FREQUENCY                  915000000  ///< Default frequency in Hz (915 MHz ISM band)
#define TX_OUTPUT_POWER               14         ///< Transmit power in dBm
#define LORA_BANDWIDTH                0          ///< LoRa bandwidth setting (0 = 125 kHz)
#define LORA_SPREADING_FACTOR         7          ///< LoRa spreading factor (SF7)
#define LORA_CODINGRATE               1          ///< LoRa coding rate (4/5)
#define LORA_PREAMBLE_LENGTH          12         ///< Preamble length in symbols
#define LORA_SYMBOL_TIMEOUT           0          ///< Symbol timeout (0 = single symbol)
#define LORA_FIX_LENGTH_PAYLOAD_ON    false      ///< Variable length payload mode
#define LORA_IQ_INVERSION_ON          false      ///< IQ inversion disabled
#define RX_TIMEOUT_VALUE              1000       ///< Default receive timeout in ms

/**
 * @class SemtechRadio
 * @brief Hardware implementation of IRadio interface for Semtech LoRa modules
 * 
 * This class provides a concrete implementation of the IRadio interface
 * specifically for Semtech LoRa radio chipsets (SX127x, SX126x series).
 * It handles all hardware-specific operations including:
 * 
 * - **Radio Initialization**: Configures the radio with appropriate LoRa parameters
 * - **Event Handling**: Processes transmission and reception events asynchronously
 * - **Power Management**: Manages transmit power and sleep modes
 * - **Signal Quality**: Provides RSSI and SNR measurements
 * 
 * The implementation uses event-driven callbacks to handle radio operations
 * efficiently without blocking the main application thread.
 * 
 * @par Configuration:
 * The radio is configured for peer-to-peer communication with parameters
 * optimized for good range and data rate balance:
 * - Frequency: 915 MHz (configurable)
 * - Bandwidth: 125 kHz
 * - Spreading Factor: SF7
 * - Coding Rate: 4/5
 * - TX Power: 14 dBm
 * 
 * @par Thread Safety:
 * This implementation uses a global instance pattern and is not thread-safe.
 * Only one SemtechRadio instance should be active at a time.
 * 
 * @par Usage Example:
 * @code
 * SemtechRadio radio(915000000);
 * if (radio.begin()) {
 *     uint8_t data[] = "Hello";
 *     radio.send(data, 5);
 *     
 *     uint8_t buffer[100];
 *     int len = radio.receive(buffer, 100, 1000);
 *     if (len > 0) {
 *         // Process received data
 *         int rssi = radio.packetRssi();
 *         float snr = radio.packetSnr();
 *     }
 * }
 * @endcode
 */
class SemtechRadio : public IRadio {
public:
    /**
     * @brief Constructor for SemtechRadio
     * @param frequency Operating frequency in Hz (default: 915 MHz)
     * 
     * Creates a new SemtechRadio instance configured for the specified frequency.
     * The frequency should be appropriate for your region's ISM band regulations:
     * - 915 MHz: North America, Australia
     * - 868 MHz: Europe
     * - 433 MHz: Asia, some European regions
     */
    SemtechRadio(uint32_t frequency = RF_FREQUENCY);

    /**
     * @brief Initialize the Semtech LoRa radio hardware
     * @return true if initialization successful, false on failure
     * 
     * Initializes the radio hardware with the configured parameters:
     * - Sets up radio callbacks for event handling
     * - Configures LoRa modulation parameters
     * - Sets transmit power and frequency
     * - Prepares radio for operation
     * 
     * This method must be called before any other radio operations.
     * 
     * @note The radio must be properly connected and powered before calling this method
     */
    bool begin() override;

    /**
     * @brief Transmit data over the LoRa radio
     * @param data Pointer to data buffer to transmit
     * @param length Number of bytes to transmit
     * @return true if transmission initiated successfully, false on error
     * 
     * Initiates transmission of the specified data. The method returns immediately
     * after starting the transmission; completion is handled via callbacks.
     * 
     * @note Maximum data length is limited by LoRa packet size constraints
     * @note The radio must be initialized with begin() before calling this method
     */
    bool send(const uint8_t* data, size_t length) override;

    /**
     * @brief Receive data from the LoRa radio
     * @param buffer Buffer to store received data
     * @param maxLen Maximum number of bytes to receive
     * @param timeoutMs Timeout for receive operation in milliseconds
     * @return Number of bytes received, 0 if timeout or no data, negative on error
     * 
     * Attempts to receive data within the specified timeout period. The method
     * will block until data is received or the timeout expires.
     * 
     * @note The radio must be initialized with begin() before calling this method
     * @note RSSI and SNR values are updated when a packet is successfully received
     */
    int receive(uint8_t* buffer, size_t maxLen, unsigned long timeoutMs = RX_TIMEOUT_VALUE) override;

    /**
     * @brief Get RSSI of the last received packet
     * @return RSSI value in dBm
     * 
     * Returns the Received Signal Strength Indicator of the most recently
     * received packet. This value is updated each time a packet is successfully
     * received and can be used for link quality assessment.
     * 
     * @note Valid only after a successful packet reception
     */
    int packetRssi() override;

    /**
     * @brief Get SNR of the last received packet
     * @return SNR value in dB
     * 
     * Returns the Signal-to-Noise Ratio of the most recently received packet.
     * This provides additional information about the quality of the radio link
     * and can be used alongside RSSI for comprehensive link analysis.
     * 
     * @note Valid only after a successful packet reception
     */
    float packetSnr() override;

private:
    uint32_t _frequency;    ///< Operating frequency in Hz
    bool _rxDoneFlag;       ///< Flag indicating reception completion
    bool _txDoneFlag;       ///< Flag indicating transmission completion
    int16_t _lastRssi;      ///< RSSI of last received packet
    int8_t _lastSnr;        ///< SNR of last received packet
    size_t _rxLen;          ///< Length of last received packet
    uint8_t _rxBuffer[BUFFER_SIZE]; ///< Internal receive buffer
    RadioEvents_t _events;  ///< Radio event callback structure

    /**
     * @brief Get current time in milliseconds (platform-specific)
     * @return Current time in milliseconds
     * 
     * Platform-specific implementation for getting current time.
     * Should be replaced with appropriate platform function.
     */
    uint32_t _get_time_ms();

    // --- Static Event Handlers ---
    // These methods use a global instance pattern to interface with the
    // Semtech library's C-style callback system
    
    static SemtechRadio* _globalInstance;  ///< Global instance for callback forwarding

    /**
     * @brief Static callback for transmission completion
     * 
     * Called by the Semtech library when transmission completes successfully.
     * Forwards the event to the appropriate instance method.
     */
    static void onTxDoneStatic();

    /**
     * @brief Static callback for transmission timeout
     * 
     * Called by the Semtech library when transmission times out.
     * Forwards the event to the appropriate instance method.
     */
    static void onTxTimeoutStatic();

    /**
     * @brief Static callback for reception completion
     * @param payload Pointer to received data
     * @param size Number of bytes received
     * @param rssi RSSI of received packet
     * @param snr SNR of received packet
     * 
     * Called by the Semtech library when a packet is successfully received.
     * Forwards the event and data to the appropriate instance method.
     */
    static void onRxDoneStatic(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr);

    /**
     * @brief Static callback for reception timeout
     * 
     * Called by the Semtech library when reception times out.
     * Forwards the event to the appropriate instance method.
     */
    static void onRxTimeoutStatic();

    /**
     * @brief Static callback for reception error
     * 
     * Called by the Semtech library when a reception error occurs.
     * Forwards the event to the appropriate instance method.
     */
    static void onRxErrorStatic();
};

/**
 * @brief Get singleton radio instance
 * @return Pointer to global SemtechRadio instance
 * 
 * Provides access to a global SemtechRadio instance using singleton pattern.
 * This function facilitates integration with systems that expect a single
 * radio instance.
 * 
 * @note This function may return nullptr if no radio has been created
 */
IRadio* GetRadio();

#endif // SEMTECH_RADIO_H
