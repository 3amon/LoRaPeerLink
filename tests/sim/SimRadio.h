/**
 * @file SimRadio.h
 * @brief Hardware-faithful LoRa radio simulator for host-side tests.
 *
 * MockRadio (tests/MockRadio.h) is a single shared FIFO: packets arrive
 * instantly, a sender can read back its own packet, nothing is ever lost and
 * time does not exist. Protocol bugs that only show up on a real half-duplex
 * radio are invisible to it.
 *
 * This simulator models what the library actually sees on an SX126x/SX127x:
 *
 *   - Virtual time. Every node runs its (blocking) program in its own thread,
 *     but only one thread runs at a time and the clock only advances when all
 *     of them are blocked in send()/receive()/sleep(). Runs are deterministic.
 *   - Time on air computed from the LoRa modem settings (Semtech AN1200.13).
 *   - Half duplex: a node cannot hear anything while it transmits.
 *   - Collisions: overlapping transmissions destroy each other at a receiver.
 *   - Two receive models, matching the two ways a driver can be written:
 *       Windowed   - the radio only listens inside receive(); a packet is
 *                    heard only if one receive() call covers it from the
 *                    preamble to the last symbol. This is how the original
 *                    SemtechRadio driver behaves.
 *       Continuous - the radio listens whenever it is not transmitting and
 *                    keeps the last packet(s) in a small buffer.
 *   - Optional random loss, topology (who can hear whom) and a per-delivery
 *     hook to drop or corrupt packets.
 */
#ifndef SIM_RADIO_H
#define SIM_RADIO_H

#include "IRadio.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace sim {

/** LoRa modem settings used to compute time on air. */
struct LoRaParams {
    int sf = 7;                  ///< Spreading factor 6..12
    int bandwidthHz = 125000;    ///< Bandwidth in Hz
    int codingRate = 1;          ///< 1 = 4/5 ... 4 = 4/8
    int preambleSymbols = 12;    ///< Programmed preamble length
    bool explicitHeader = true;
    bool crcOn = true;
    bool lowDataRateOptimize = false;
};

/** Time on air in microseconds for a payload of @p payloadLen bytes. */
int64_t airtimeUs(const LoRaParams& p, size_t payloadLen);

/** Duration of one LoRa symbol in microseconds. */
int64_t symbolUs(const LoRaParams& p);

enum class RxMode { Windowed, Continuous };

/** One transmission on the shared medium. */
struct Transmission {
    int src = -1;                 ///< Index of the transmitting radio
    int64_t startUs = 0;
    int64_t endUs = 0;
    std::vector<uint8_t> bytes;
    int deliveredTo = 0;          ///< Number of radios that received it intact
    int collidedAt = 0;           ///< Number of radios where it was destroyed by a collision
    int missedBy = 0;             ///< Number of radios that were in range but not listening
    /** Per radio index: 'D' delivered, 'C' collision, 'M' not listening, 'L' lost (random loss or filter), '-' out of range or the sender. */
    std::string outcome;
};

class World;

/** Thrown inside node programs when the world stops (time limit reached). */
struct SimStop {};

class SimRadio : public IRadio {
public:
    bool begin() override;
    bool send(const uint8_t* data, size_t length) override;
    int receive(uint8_t* buffer, size_t maxLength, unsigned long timeoutMs = 1000) override;
    int packetRssi() override { return _lastRssi; }
    float packetSnr() override { return _lastSnr; }
    uint32_t timeOnAirMs(size_t length) override;

    /** Make timeOnAirMs() return 0, like a driver that does not implement it. */
    void setReportsAirtime(bool on) { _reportsAirtime = on; }

    int index() const { return _index; }
    RxMode mode() const { return _mode; }

    // Counters (read them after World::run()).
    int txCount = 0;        ///< Packets transmitted
    int rxCount = 0;        ///< Packets handed to the caller of receive()
    int rxOverflow = 0;     ///< Continuous mode: packets dropped because the buffer was full
    int receiveCalls = 0;   ///< Number of receive() calls
    int64_t listenUs = 0;   ///< Total time spent inside receive()

    /** One receive() call, for debugging a scenario. Only filled when recordWindows is true. */
    struct Window { int64_t startUs; int64_t endUs; int result; };
    bool recordWindows = false;
    std::vector<Window> windows;

private:
    friend class World;
    SimRadio(World* w, int index, RxMode mode, size_t bufferSlots)
        : _world(w), _index(index), _mode(mode), _bufferSlots(bufferSlots) {}

    World* _world;
    int _index;
    RxMode _mode;
    size_t _bufferSlots;
    bool _begun = false;
    bool _reportsAirtime = true;

    // Receive state (guarded by the world mutex)
    bool _listening = false;
    int64_t _listenStartUs = 0;
    bool _rxReady = false;
    std::vector<uint8_t> _rxPacket;
    std::deque<std::vector<uint8_t>> _buffer;   // Continuous mode
    int _taskId = -1;                           // Task currently blocked in receive()
    int _lastRssi = -60;
    float _lastSnr = 9.5f;
};

/**
 * The simulated world: clock, scheduler, medium and radios.
 *
 * Typical use:
 * @code
 *   sim::World world;
 *   auto& ra = world.addRadio();
 *   auto& rb = world.addRadio();
 *   world.spawn([&] { ... node A program using ra ... });
 *   world.spawn([&] { ... node B program using rb ... });
 *   world.run(10000);          // at most 10 virtual seconds
 * @endcode
 *
 * Node programs get the time and sleep callbacks from sim::nowMs() and
 * sim::sleepMs(), which are plain functions and can be passed to the library.
 * Do not use Catch2 assertions inside node programs (Catch2 is not thread
 * safe); record results and assert after run().
 */
class World {
public:
    explicit World(uint64_t seed = 1, LoRaParams params = LoRaParams());
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    SimRadio& addRadio(RxMode mode = RxMode::Windowed, size_t bufferSlots = 1);

    /** Start a node program at virtual time @p startDelayMs. */
    void spawn(std::function<void()> program, uint32_t startDelayMs = 0);

    /**
     * Run until every program has returned or @p maxVirtualMs of virtual time
     * has passed (programs still running are then unwound with SimStop).
     * @return true if all programs returned on their own.
     */
    bool run(uint32_t maxVirtualMs);

    /** Probability that an otherwise good reception is lost. */
    void setLossProbability(double p) { _lossProbability = p; }

    /** Say whether radio @p a and radio @p b can hear each other (default: yes). */
    void setConnected(int a, int b, bool connected);

    /**
     * Hook called for every reception that would succeed. Return false to drop
     * the packet; the bytes may be modified to simulate corruption.
     */
    using DeliveryFilter = std::function<bool(const Transmission& tx, int rxIndex, std::vector<uint8_t>& bytes)>;
    void setDeliveryFilter(DeliveryFilter f) { _deliveryFilter = std::move(f); }

    /** Hook called when a transmission starts (for logging and assertions). */
    using TxObserver = std::function<void(const Transmission& tx)>;
    void setTxObserver(TxObserver f) { _txObserver = std::move(f); }

    const LoRaParams& params() const { return _params; }
    const std::vector<Transmission>& transmissions() const { return _transmissions; }
    int64_t nowUs() const { return _nowUs; }

    /** Fixed delay between send() being called and the first symbol on air. */
    void setTxSetupUs(int64_t us) { _txSetupUs = us; }

    /** Deterministic per-world random source for tests. */
    std::mt19937_64& rng() { return _rng; }

    /** Exceptions (other than SimStop) that escaped node programs. Empty on a healthy run. */
    const std::vector<std::string>& errors() const { return _errors; }

private:
    friend class SimRadio;
    friend uint32_t nowMs();
    friend void sleepMs(uint32_t);
    friend uint16_t rand16();

    struct Task {
        int id = 0;
        std::function<void()> program;
        std::thread thread;
        int64_t wakeUs = 0;
        int priority = 1;       // 0 runs before 1 at the same instant
        uint64_t order = 0;     // FIFO tie break
        bool finished = false;
        std::mt19937 rng;       // per node random source
    };

    // All of these require the world mutex.
    void blockUntil(std::unique_lock<std::mutex>& lock, Task& self, int64_t wakeUs, int priority);
    void scheduleNext();
    void finishTransmission(size_t txIndex);
    bool overlapsOther(const Transmission& tx, size_t txIndex, int rxIndex) const;
    bool wasTransmitting(int radioIndex, int64_t fromUs, int64_t toUs) const;
    bool connected(int a, int b) const;
    Task& currentTask();

    LoRaParams _params;
    std::mt19937_64 _rng;
    double _lossProbability = 0.0;
    int64_t _txSetupUs = 1000;
    DeliveryFilter _deliveryFilter;
    TxObserver _txObserver;

    std::mutex _mutex;
    std::condition_variable _cv;
    std::vector<std::unique_ptr<Task>> _tasks;
    std::vector<std::unique_ptr<SimRadio>> _radios;
    std::vector<Transmission> _transmissions;
    std::vector<std::string> _errors;
    std::vector<std::vector<bool>> _disconnected;
    int64_t _nowUs = 0;
    int64_t _limitUs = 0;
    int _running = -1;          // id of the task allowed to run, -1 = none
    bool _stopping = false;
    bool _started = false;
    uint64_t _orderCounter = 0;
    uint64_t _seed;
};

/** Current virtual time in milliseconds. Usable as ILoRaLink::time_ms_fn. */
uint32_t nowMs();

/** Block the calling node for @p ms of virtual time. Usable as sleep_ms_fn. */
void sleepMs(uint32_t ms);

/** Per-node deterministic 16-bit random number. Usable as RollCall::random_fn. */
uint16_t rand16();

} // namespace sim

#endif // SIM_RADIO_H
