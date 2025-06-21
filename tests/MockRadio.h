#ifndef MOCK_RADIO_H
#define MOCK_RADIO_H

#include "IRadio.h"
#include <queue>
#include <vector>
#include <cstdint>
#include <iostream>

class MockRadio : public IRadio {
public:
    struct Packet {
        std::vector<uint8_t> data;
        int rssi = -42;
        float snr = 10.0;
    };

    bool begin() override {
        return true;
    }

    bool send(const uint8_t* data, size_t length) override {
        std::cout << "[MockRadio] send() called with size: " << length << std::endl;
        Packet p;
        p.data.assign(data, data + length);
        _globalAir.push(p);
        return true;
    }

    int receive(uint8_t* buffer, size_t maxLength, unsigned long timeoutMs = 1000) override {
        std::cout << "[MockRadio] receive() called with maxLength: " << maxLength << std::endl;
        if (_globalAir.empty()) return 0;

        Packet p = _globalAir.front();
        _globalAir.pop();

        if (p.data.size() > maxLength) return 0;

        std::copy(p.data.begin(), p.data.end(), buffer);
        return p.data.size();
    }

    int packetRssi() override { return -42; }
    float packetSnr() override { return 10.0; }

    static void clearChannel() {
        std::queue<Packet> empty;
        std::swap(_globalAir, empty);
    }

private:
    static std::queue<Packet> _globalAir;  // shared by all nodes
};

// Static member
inline std::queue<MockRadio::Packet> MockRadio::_globalAir;

#endif // MOCK_RADIO_H
