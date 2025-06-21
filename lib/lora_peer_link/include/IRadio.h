#ifndef IRADIO_H
#define IRADIO_H

#define BUFFER_SIZE 256

class IRadio {
public:
    virtual ~IRadio() {}

    // Initialize the radio. Return true on success.
    virtual bool begin() = 0;

    // Send raw data. Return true on success.
    virtual bool send(const uint8_t* data, size_t length) = 0;

    // Receive data into buffer, return number of bytes received.
    virtual int receive(uint8_t* buffer, size_t maxLength, unsigned long timeoutMs = 1000) = 0;

    // Optional: RSSI and SNR of last received packet
    virtual int packetRssi() = 0;
    virtual float packetSnr() = 0;
};

#endif // IRADIO_H
