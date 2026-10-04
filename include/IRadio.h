/**
 * @file IRadio.h
 * @brief Abstract radio interface for LoRa communication
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file defines the abstract interface for radio communication devices.
 * It provides a hardware abstraction layer that allows the LoRaPeerLink
 * library to work with different radio implementations (hardware or mock).
 */

#ifndef IRADIO_H
#define IRADIO_H

#define BUFFER_SIZE 256  ///< Maximum buffer size for radio operations

#include <cstdint>
#include <cstddef>

/**
 * @class IRadio
 * @brief Abstract interface for radio communication devices
 * 
 * This pure virtual class defines the interface that all radio implementations
 * must provide. It abstracts the underlying radio hardware or simulation,
 * allowing higher-level protocols to work with any compatible radio device.
 * 
 * Implementations include:
 * - SemtechRadio: For real hardware communication (see esp32_platformio example)
 * - MockRadio: For testing and simulation
 */
class IRadio {
public:
    /**
     * @brief Virtual destructor for proper cleanup of derived classes
     */
    virtual ~IRadio() {}

    /**
     * @brief Initialize the radio hardware/simulation
     * @return true if initialization was successful, false otherwise
     * 
     * This method must be called before any other radio operations.
     * It should configure the radio parameters, frequencies, and 
     * prepare the device for transmission and reception.
     */
    virtual bool begin() = 0;

    /**
     * @brief Send raw data over the radio
     * @param data Pointer to the data buffer to transmit
     * @param length Number of bytes to transmit
     * @return true if transmission was successful, false otherwise
     * 
     * Transmits the specified data over the radio. The implementation
     * should handle all low-level radio operations including modulation,
     * error checking, and transmission timing.
     */
    virtual bool send(const uint8_t* data, size_t length) = 0;

    /**
     * @brief Receive data from the radio
     * @param buffer Buffer to store received data
     * @param maxLength Maximum number of bytes that can be stored in buffer
     * @param timeoutMs Timeout for receive operation in milliseconds (default: 1000ms)
     * @return Number of bytes actually received, 0 if no data or timeout, negative on error
     * 
     * Attempts to receive data from the radio within the specified timeout.
     * The method should return immediately if data is available, or wait
     * up to timeoutMs milliseconds for data to arrive.
     *
     * Contract the link layer relies on:
     * - Block until a frame has been received or timeoutMs has passed. A
     *   return value of 0 means "nothing arrived in this window".
     * - Do not abort a frame that is already being received when the timeout
     *   expires, if the hardware allows it.
     * - Ideally keep the receiver running between calls (continuous receive)
     *   and return a frame that completed since the previous call. A driver
     *   that only listens inside this call still works, but every frame that
     *   arrives outside a call is lost.
     */
    virtual int receive(uint8_t* buffer, size_t maxLength, unsigned long timeoutMs = 1000) = 0;

    /**
     * @brief Get the RSSI (Received Signal Strength Indicator) of the last received packet
     * @return RSSI value in dBm, or implementation-specific error value
     * 
     * Returns the signal strength of the most recently received packet.
     * This is useful for link quality assessment and debugging.
     */
    virtual int packetRssi() = 0;

    /**
     * @brief Get the SNR (Signal-to-Noise Ratio) of the last received packet
     * @return SNR value in dB, or implementation-specific error value
     * 
     * Returns the signal-to-noise ratio of the most recently received packet.
     * This provides additional information about the quality of the radio link.
     */
    virtual float packetSnr() = 0;

    /**
     * @brief Time on air of a frame of @p length bytes with the current modem settings
     * @param length Frame length in bytes
     * @return Time on air in milliseconds, or 0 if the driver does not know
     *
     * Optional. The link layer uses it to make sure the ACK timeout is long
     * enough for slow settings (an ACK takes about 50 ms at SF7/125 kHz but
     * well over a second at SF12). Drivers that return 0 leave the timeout
     * exactly as configured with LoRaLinkCore::setAckTimeoutMs().
     */
    virtual uint32_t timeOnAirMs(size_t length) { (void)length; return 0; }
};

#endif // IRADIO_H
