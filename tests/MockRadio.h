#ifndef MOCK_RADIO_H
#define MOCK_RADIO_H

#include "IRadio.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

/**
 * Minimal radio mock for unit tests that do not need timing.
 *
 * Every MockRadio is attached to one shared "channel". A packet sent by one
 * radio is delivered, instantly and without loss, to the inbox of every other
 * radio. A radio never hears its own transmission, just like real hardware.
 * receive() never blocks: it returns the oldest packet in the inbox or 0.
 *
 * There is no notion of time, airtime, collisions or deafness here. Tests
 * that depend on those use the simulator in tests/sim/SimRadio.h instead.
 */
class MockRadio : public IRadio {
public:
    struct Packet {
        std::vector<uint8_t> data;
        int rssi = -42;
        float snr = 10.0;
    };

    MockRadio() { radios().push_back(this); }
    ~MockRadio() override {
        auto& all = radios();
        all.erase(std::remove(all.begin(), all.end(), this), all.end());
    }
    MockRadio(const MockRadio&) = delete;
    MockRadio& operator=(const MockRadio&) = delete;

    bool begin() override {
        return true;
    }

    bool send(const uint8_t* data, size_t length) override {
        Packet p;
        p.data.assign(data, data + length);
        for (MockRadio* r : radios()) {
            if (r != this) r->_inbox.push_back(p);
        }
        ++sentCount;
        return true;
    }

    int receive(uint8_t* buffer, size_t maxLength, unsigned long timeoutMs = 1000) override {
        (void)timeoutMs;
        if (_inbox.empty()) return 0;

        Packet p = _inbox.front();
        _inbox.pop_front();

        if (p.data.size() > maxLength) return 0;

        std::copy(p.data.begin(), p.data.end(), buffer);
        return static_cast<int>(p.data.size());
    }

    int packetRssi() override { return -42; }
    float packetSnr() override { return 10.0; }

    /** Empty the inbox of every radio. Call at the start of each test. */
    static void clearChannel() {
        for (MockRadio* r : radios()) r->_inbox.clear();
    }

    /** Put a packet into this radio's inbox, as if some other node had sent it. */
    void injectPacket(const uint8_t* data, size_t length) {
        Packet p;
        p.data.assign(data, data + length);
        _inbox.push_back(p);
    }

    /** Number of packets waiting in this radio's inbox. */
    size_t pending() const { return _inbox.size(); }

    int sentCount = 0;   ///< Packets this radio has transmitted

private:
    static std::vector<MockRadio*>& radios() {
        static std::vector<MockRadio*> all;
        return all;
    }

    std::deque<Packet> _inbox;
};

#endif // MOCK_RADIO_H
