#include "SimRadio.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sim {

namespace {
thread_local World* t_world = nullptr;
thread_local void* t_task = nullptr;

// Transmissions are stored in start order. Nothing on air lasts longer than
// this, so scans for overlaps can stop once they are this far in the past.
const int64_t kMaxAirtimeUs = 20LL * 1000 * 1000;
} // namespace

int64_t symbolUs(const LoRaParams& p) {
    return static_cast<int64_t>(std::llround((static_cast<double>(1 << p.sf) / p.bandwidthHz) * 1e6));
}

int64_t airtimeUs(const LoRaParams& p, size_t payloadLen) {
    // Semtech AN1200.13, section 4.
    const double tSym = static_cast<double>(1 << p.sf) / p.bandwidthHz;
    const double tPreamble = (p.preambleSymbols + 4.25) * tSym;
    const int de = p.lowDataRateOptimize ? 1 : 0;
    const int ih = p.explicitHeader ? 0 : 1;
    const int crc = p.crcOn ? 1 : 0;
    const double num = 8.0 * static_cast<double>(payloadLen) - 4.0 * p.sf + 28.0 + 16.0 * crc - 20.0 * ih;
    const double den = 4.0 * (p.sf - 2 * de);
    const double extra = std::max(std::ceil(num / den) * (p.codingRate + 4), 0.0);
    const double tPayload = (8.0 + extra) * tSym;
    return static_cast<int64_t>(std::llround((tPreamble + tPayload) * 1e6));
}

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------

World::World(uint64_t seed, LoRaParams params) : _params(params), _rng(seed), _seed(seed) {}

World::~World() {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _stopping = true;
        _cv.notify_all();
    }
    for (auto& t : _tasks) {
        if (t->thread.joinable()) t->thread.join();
    }
}

SimRadio& World::addRadio(RxMode mode, size_t bufferSlots) {
    std::unique_lock<std::mutex> lock(_mutex);
    int index = static_cast<int>(_radios.size());
    _radios.emplace_back(new SimRadio(this, index, mode, bufferSlots == 0 ? 1 : bufferSlots));
    for (auto& row : _disconnected) row.push_back(false);
    _disconnected.emplace_back(_radios.size(), false);
    return *_radios.back();
}

void World::setConnected(int a, int b, bool isConnected) {
    std::unique_lock<std::mutex> lock(_mutex);
    _disconnected.at(a).at(b) = !isConnected;
    _disconnected.at(b).at(a) = !isConnected;
}

bool World::connected(int a, int b) const {
    return !_disconnected[a][b];
}

void World::spawn(std::function<void()> program, uint32_t startDelayMs) {
    std::unique_lock<std::mutex> lock(_mutex);
    if (_started) throw std::logic_error("sim::World::spawn called after run()");
    std::unique_ptr<Task> task(new Task());
    task->id = static_cast<int>(_tasks.size());
    task->program = std::move(program);
    task->wakeUs = static_cast<int64_t>(startDelayMs) * 1000;
    task->order = ++_orderCounter;
    task->rng.seed(static_cast<uint32_t>(_seed * 2654435761u + 0x9E3779B9u * (task->id + 1)));
    _tasks.push_back(std::move(task));
}

World::Task& World::currentTask() {
    if (t_world != this || t_task == nullptr) {
        throw std::logic_error("sim radio/time call made outside a node program");
    }
    return *static_cast<Task*>(t_task);
}

void World::scheduleNext() {
    Task* best = nullptr;
    for (auto& t : _tasks) {
        if (t->finished) continue;
        if (!best || t->wakeUs < best->wakeUs ||
            (t->wakeUs == best->wakeUs && (t->priority < best->priority ||
                                           (t->priority == best->priority && t->order < best->order)))) {
            best = t.get();
        }
    }
    if (!best) {
        _running = -1;
    } else if (best->wakeUs > _limitUs) {
        _stopping = true;
        _running = -1;
    } else {
        _nowUs = std::max(_nowUs, best->wakeUs);
        _running = best->id;
    }
    _cv.notify_all();
}

void World::blockUntil(std::unique_lock<std::mutex>& lock, Task& self, int64_t wakeUs, int priority) {
    if (_stopping) throw SimStop();
    self.wakeUs = std::max(wakeUs, _nowUs);
    self.priority = priority;
    self.order = ++_orderCounter;
    scheduleNext();
    _cv.wait(lock, [&] { return _running == self.id || _stopping; });
    if (_stopping) throw SimStop();
}

bool World::run(uint32_t maxVirtualMs) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_started) throw std::logic_error("sim::World::run called twice");
        _started = true;
        _limitUs = static_cast<int64_t>(maxVirtualMs) * 1000;

        for (auto& tp : _tasks) {
            Task* task = tp.get();
            task->thread = std::thread([this, task] {
                {
                    std::unique_lock<std::mutex> l(_mutex);
                    t_world = this;
                    t_task = task;
                    _cv.wait(l, [&] { return _running == task->id || _stopping; });
                    if (_stopping) {
                        task->finished = true;
                        _cv.notify_all();
                        return;
                    }
                }
                try {
                    task->program();
                } catch (const SimStop&) {
                    // World stopped while this node was blocked.
                } catch (const std::exception& e) {
                    std::unique_lock<std::mutex> l(_mutex);
                    _errors.push_back(std::string("node program ") + std::to_string(task->id) +
                                      " threw: " + e.what());
                } catch (...) {
                    std::unique_lock<std::mutex> l(_mutex);
                    _errors.push_back(std::string("node program ") + std::to_string(task->id) +
                                      " threw an unknown exception");
                }
                std::unique_lock<std::mutex> l(_mutex);
                task->finished = true;
                if (!_stopping) scheduleNext();
                _cv.notify_all();
            });
        }

        scheduleNext();
        _cv.wait(lock, [&] {
            for (auto& t : _tasks) {
                if (!t->finished) return false;
            }
            return true;
        });
    }
    for (auto& t : _tasks) {
        if (t->thread.joinable()) t->thread.join();
    }
    return !_stopping;
}

bool World::wasTransmitting(int radioIndex, int64_t fromUs, int64_t toUs) const {
    for (size_t i = _transmissions.size(); i-- > 0;) {
        const Transmission& t = _transmissions[i];
        if (t.startUs + kMaxAirtimeUs < fromUs) break;
        // The radio is busy from the moment send() is called (setup) until the
        // last symbol has left.
        if (t.src == radioIndex && (t.startUs - _txSetupUs) < toUs && t.endUs > fromUs) return true;
    }
    return false;
}

bool World::overlapsOther(const Transmission& tx, size_t txIndex, int rxIndex) const {
    for (size_t i = _transmissions.size(); i-- > 0;) {
        if (i == txIndex) continue;
        const Transmission& t = _transmissions[i];
        if (t.startUs + kMaxAirtimeUs < tx.startUs) break;
        if (t.src == rxIndex || !connected(t.src, rxIndex)) continue;
        if (t.startUs < tx.endUs && t.endUs > tx.startUs) return true;
    }
    return false;
}

void World::finishTransmission(size_t txIndex) {
    // A receiver has to catch enough of the preamble to synchronise. Allow it
    // to start listening until the last 6 preamble symbols.
    const int64_t slackUs = std::max<int64_t>(0, (_params.preambleSymbols - 6)) * symbolUs(_params);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    _transmissions[txIndex].outcome.assign(_radios.size(), '-');
    for (auto& rp : _radios) {
        SimRadio& r = *rp;
        Transmission& tx = _transmissions[txIndex];
        if (r._index == tx.src || !connected(tx.src, r._index)) continue;

        bool heard;
        if (r._mode == RxMode::Windowed) {
            heard = r._listening && !r._rxReady && r._listenStartUs <= tx.startUs + slackUs;
        } else {
            heard = !wasTransmitting(r._index, tx.startUs, tx.endUs);
        }
        if (!heard) {
            ++tx.missedBy;
            tx.outcome[r._index] = 'M';
            continue;
        }
        if (overlapsOther(tx, txIndex, r._index)) {
            ++tx.collidedAt;
            tx.outcome[r._index] = 'C';
            continue;
        }
        if (_lossProbability > 0.0 && uniform(_rng) < _lossProbability) {
            ++tx.missedBy;
            tx.outcome[r._index] = 'L';
            continue;
        }
        std::vector<uint8_t> bytes = tx.bytes;
        if (_deliveryFilter && !_deliveryFilter(tx, r._index, bytes)) {
            ++tx.missedBy;
            tx.outcome[r._index] = 'L';
            continue;
        }

        ++_transmissions[txIndex].deliveredTo;
        _transmissions[txIndex].outcome[r._index] = 'D';
        if (r._listening && !r._rxReady) {
            r._rxPacket = std::move(bytes);
            r._rxReady = true;
            Task& waiter = *_tasks[r._taskId];
            waiter.wakeUs = _nowUs;
            waiter.priority = 0;
        } else {
            // Continuous mode, caller is not inside receive(): keep it for later.
            if (r._buffer.size() >= r._bufferSlots) {
                r._buffer.pop_front();
                ++r.rxOverflow;
            }
            r._buffer.push_back(std::move(bytes));
        }
    }
}

// ---------------------------------------------------------------------------
// SimRadio
// ---------------------------------------------------------------------------

bool SimRadio::begin() {
    _begun = true;
    return true;
}

uint32_t SimRadio::timeOnAirMs(size_t length) {
    if (!_reportsAirtime) return 0;
    return static_cast<uint32_t>((airtimeUs(_world->_params, length) + 999) / 1000);
}

bool SimRadio::send(const uint8_t* data, size_t length) {
    World& w = *_world;
    std::unique_lock<std::mutex> lock(w._mutex);
    World::Task& self = w.currentTask();
    if (data == nullptr || length == 0 || length > 255) return false;

    Transmission tx;
    tx.src = _index;
    tx.startUs = w._nowUs + w._txSetupUs;
    tx.endUs = tx.startUs + airtimeUs(w._params, length);
    tx.bytes.assign(data, data + length);
    const size_t txIndex = w._transmissions.size();
    w._transmissions.push_back(tx);
    ++txCount;
    if (w._txObserver) w._txObserver(w._transmissions[txIndex]);

    w.blockUntil(lock, self, tx.endUs, 0);
    w.finishTransmission(txIndex);
    return true;
}

int SimRadio::receive(uint8_t* buffer, size_t maxLength, unsigned long timeoutMs) {
    World& w = *_world;
    std::unique_lock<std::mutex> lock(w._mutex);
    World::Task& self = w.currentTask();
    ++receiveCalls;
    const int64_t callStartUs = w._nowUs;

    std::vector<uint8_t> packet;
    bool got = false;

    if (_mode == RxMode::Continuous && !_buffer.empty()) {
        packet = std::move(_buffer.front());
        _buffer.pop_front();
        got = true;
    } else {
        _listening = true;
        _rxReady = false;
        _listenStartUs = w._nowUs;
        _taskId = self.id;
        const int64_t start = w._nowUs;
        try {
            w.blockUntil(lock, self, w._nowUs + static_cast<int64_t>(timeoutMs) * 1000, 1);
        } catch (...) {
            _listening = false;
            throw;
        }
        _listening = false;
        listenUs += w._nowUs - start;
        if (_rxReady) {
            packet = std::move(_rxPacket);
            _rxReady = false;
            got = true;
        }
    }

    int result = 0;
    if (got && packet.size() <= maxLength) {   // Too large: silently dropped, like the hardware driver
        std::copy(packet.begin(), packet.end(), buffer);
        ++rxCount;
        result = static_cast<int>(packet.size());
    }
    if (recordWindows) windows.push_back({callStartUs, w._nowUs, result});
    return result;
}

// ---------------------------------------------------------------------------
// Free functions handed to the library as callbacks
// ---------------------------------------------------------------------------

uint32_t nowMs() {
    World* w = t_world;
    if (!w) return 0;
    return static_cast<uint32_t>(w->_nowUs / 1000);
}

void sleepMs(uint32_t ms) {
    World* w = t_world;
    if (!w) throw std::logic_error("sim::sleepMs called outside a node program");
    std::unique_lock<std::mutex> lock(w->_mutex);
    World::Task& self = w->currentTask();
    w->blockUntil(lock, self, w->_nowUs + static_cast<int64_t>(ms) * 1000, 1);
}

uint16_t rand16() {
    World* w = t_world;
    if (!w) throw std::logic_error("sim::rand16 called outside a node program");
    World::Task& self = w->currentTask();
    return static_cast<uint16_t>(self.rng());
}

} // namespace sim
