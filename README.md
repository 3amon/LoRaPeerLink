# LoRaPeerLink

**LoRaPeerLink** is a lightweight, layered protocol framework designed for low-level LoRa communication between nearby nodes without the overhead of LoRaWAN or routing. It includes a modular radio interface, multiple link-layer strategies, and a simulation/test environment using Catch2 and mock radios.

---

## 🌐 Project Structure

```
LoRaPeerLink/
├── include/
│ └── lora_peer_link/
│ ├── IRadio.h
│ ├── LoRaBasicLink.h
│ ├── WeaklyBackoffLink.h
│ └── MockRadio.h
├── lib/
│ └── lora_peer_link/
│ ├── include/...
│ └── src/
│ └── SemtechRadio.cpp
├── tests/
│ ├── test_simple_link.cpp
│ └── test_backoff_link.cpp
├── CMakeLists.txt
└── README.md
```

---

## 📦 Layered Architecture

### 1. **IRadio Interface**
An abstract C++ interface used to decouple radio drivers from higher layers.

```cpp
class IRadio {
public:
    virtual bool send(const uint8_t* data, size_t len) = 0;
    virtual int receive(uint8_t* buffer, size_t maxLen, int timeoutMs = -1) = 0;
    virtual ~IRadio() = default;
};
```
### 2. LoRaBasicLink

A simple framing protocol with:

    Fixed-size headers

    CRC-16 integrity

    Optional ACK/Retry

### 3. WeaklyBackoffLink

An alternative link-layer that introduces basic collision avoidance by:

    Listening before sending

    Retrying with backoff (when no CAD is available)

### 🧪 Testing
MockRadio

A simulated IRadio implementation that allows two nodes to communicate through a shared virtual “air”. Only one message can be in the air at a time.
Catch2 Integration

Uses Catch2 v3 via CMake’s FetchContent to compile and run unit tests.
Test Cases

    test_simple_link.cpp: Covers LoRaBasicLink for sending/receiving and ACK handling.

    test_backoff_link.cpp: Covers WeaklyBackoffLink behavior.

Run Tests (on Ubuntu):

mkdir build && cd build
cmake ..
make
./tests/test_simple_link
./tests/test_backoff_link

### 🧠 Design Notes
Future Features (Not Yet Implemented)

    Name-to-ID Layer:

        Each node chooses a random 2-byte ID.

        Collisions are detected and resolved via backoff.

        Nodes can query names with messages like:

            WHOIS <ID>

            WHEREIS <Name>

            HELLOIAM <Name> AT <ID>

        Only human-readable strings are sent briefly in name resolution messages; otherwise, short numeric IDs are used.

    Robust Collision Management:

        Consider more advanced backoff strategies

        Fairness under high contention

    Timing Simulation:

        Currently assumes perfect timing (no partial messages, no interleaving)

        Potential future use of queues, message lifetime, or air contention simulation

### 🧰 Building on GitHub Actions

The CI system builds all test binaries and runs them on each push.

You can simulate and validate link behavior entirely in a GitHub container (Ubuntu-latest) without any hardware required.
💬 Notes

    Currently avoids all Arduino functions.

    Designed with embedded Linux or bare-metal microcontroller usage in mind.

    Perfect for unit testing link protocols before hardware integration.