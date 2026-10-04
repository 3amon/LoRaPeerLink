# LoRaPeerLink

A C++ library for direct device-to-device LoRa links, without LoRaWAN or a
gateway. Nodes find each other by name, send acknowledged messages with
retries, and can encrypt and authenticate the link.

It is hardware independent: you supply a small radio driver (`IRadio`), and
the same code runs on a microcontroller or, for testing, on a simulated radio
channel on a PC.

## Status

- **185 test cases**, most of them running the real library code on a
  simulated half-duplex LoRa channel with time on air, collisions and packet
  loss, plus seed sweeps of 200 random seeds per scenario. See
  [TEST_PLAN.md](TEST_PLAN.md).
- **Verified on hardware:** two SX1262 boards ran for 1 hour 50 minutes over a
  very weak link (about -115 dBm). In the one-hour run 1,204 of 1,209
  acknowledged pings were delivered, nothing was delivered twice, and the
  encrypted link, SF10 and SF12 all worked. Results and raw logs:
  [hardware-tests/2026-10-03-wireless-shell-v3](hardware-tests/2026-10-03-wireless-shell-v3/RESULTS.md).
- **Not yet tested:** range with antennas, more than two boards on real
  hardware, runs longer than an hour.

Version 1.0 passed all of its unit tests and did not work on real radios.
[TEST_PLAN.md](TEST_PLAN.md) explains why, and [CHANGELOG.md](CHANGELOG.md)
lists what version 2.0 fixed.

## Layers

```
Your application
  PeerMessenger        send and receive text by node name or ID
  RollCall             naming, ID assignment, discovery, collision handling
  ILoRaLink            LoRaBasicLink      acknowledgments and retries
                       LoRaBackoffLink    the same, with collision backoff
                       EncryptedLoRaLink  AES-128-CBC + HMAC-SHA256, wraps another link
  IRadio               your driver for the radio chip
```

Use the whole stack, or only the link layer.

## Quick start

```cpp
#include "PeerMessenger.h"
#include "RollCall.h"
#include "LoraBasicLink.h"
// Include your radio implementation (e.g., SemtechRadio for ESP32)

// Your timing functions (platform-specific)
uint32_t get_time_ms() { return millis(); }
void sleep_ms(uint32_t ms) { delay(ms); }
// A real random source, so that boards running the same firmware pick different IDs
uint16_t random_16() { return (uint16_t)esp_random(); }

// Create your radio implementation and link layer
MyRadio radio;  // Your IRadio implementation
LoRaBasicLink link(&radio, get_time_ms, sleep_ms);

// Create discovery and messaging layers
RollCall rollCall(&link, "sensor-01", get_time_ms, sleep_ms, random_16);
PeerMessenger messenger(&rollCall);

void setup() {
    Serial.begin(115200);
    
    // Initialize the system
    if (!radio.begin()) {
        Serial.println("Radio failed to start!");
        return;
    }
    
    rollCall.begin();
    messenger.begin();
    Serial.println("LoRa messaging ready!");
}

void loop() {
    // Listen for up to a second: handles discovery, queues incoming messages
    // and sends this node's periodic announcements. This is the only call the
    // stack needs; do not add a delay() to the loop.
    messenger.processMessages(1000);
    
    // Send a sensor reading to the gateway, with acknowledgment
    static uint32_t lastSend = 0;
    if (millis() - lastSend > 10000) {
        bool delivered = messenger.sendMessage("gateway", "Temperature: 25.3°C", true);
        Serial.println(delivered ? "Sensor data delivered" : "Gateway did not answer");
        lastSend = millis();
    }
    
    // Check for incoming messages
    while (messenger.hasMessage()) {
        UserMessage msg = messenger.receiveMessage();
        Serial.printf("From %s: %s\n", msg.srcName.c_str(), msg.content.c_str());
    }
}
```

`processMessages()` is the only call the stack needs. Call it with a long
timeout and do not sleep in the loop: the library can only hear packets while
the radio is receiving.

## Using the link layer directly

```cpp
#include "LoraBasicLink.h"      // Basic point-to-point
#include "LoraBackoffLink.h"    // Collision avoidance
#include "EncryptedLoRaLink.h"  // Secure communication

// Choose your link implementation
LoRaBasicLink basicLink(&radio, get_time_ms, sleep_ms);
// OR
LoRaBackoffLink smartLink(&radio, get_time_ms, sleep_ms);
// OR
// Wraps another link: everyone with the same network name and password can talk
EncryptedLoRaLink secureLink(&smartLink, "my-network", "my-password",
                             EncryptedLoRaLink::DEFAULT_KEY_ITERATIONS, get_time_ms);

void setup() {
    radio.begin();
    basicLink.setLocalId(1);  // Set your node ID
}

void loop() {
    // Send message
    const char* message = "Hello World";
    basicLink.sendPacket(1, 2, (uint8_t*)message, strlen(message), true);
    
    // Receive message
    uint16_t fromId;
    uint8_t buffer[MAX_PAYLOAD + 1];   // room for a terminating zero
    int len = basicLink.receivePacket(&fromId, buffer, MAX_PAYLOAD, 1000);
    if (len > 0) {
        buffer[len] = 0;
        Serial.printf("From %d: %s\n", fromId, (char*)buffer);
    }
}
```

## Writing a radio driver

The library does not talk to any radio chip itself. Implement `IRadio` for
your hardware:

```cpp
#include "IRadio.h"

class MyCustomRadio : public IRadio {
public:
    bool begin() override {
        // Initialize your radio hardware
        return true;
    }
    
    bool send(const uint8_t* data, size_t len) override {
        // Send data through your radio
        return true;
    }
    
    int receive(uint8_t* buffer, size_t maxLen, unsigned long timeoutMs = 1000) override {
        // Check for incoming data, return length or 0 if none
        return 0;
    }
    
    int packetRssi() override {
        // Return signal strength of last received packet
        return -100;
    }
    
    float packetSnr() override {
        // Return signal-to-noise ratio of last received packet  
        return 0.0;
    }
};

// Use your custom radio
MyCustomRadio myRadio;
LoRaBasicLink link(&myRadio, get_time_ms, sleep_ms);
```

Read the receive contract in [`include/IRadio.h`](include/IRadio.h) before
writing a driver: a driver that keeps the radio in continuous receive loses
the fewest packets. If the driver also implements `timeOnAirMs()`, the link
sets its acknowledgment timeout from it, which matters at slow settings.

[`examples/esp32_platformio/src/SemtechRadio.cpp`](examples/esp32_platformio/src/SemtechRadio.cpp)
is a working driver for the SX1262 on Heltec ESP32-S3 boards.

## Installation

**PlatformIO**

```ini
[env:your_board]
lib_deps = https://github.com/3amon/LoRaPeerLink.git
```

**Arduino IDE:** download the repository as a ZIP and extract it into your
`libraries/` folder.

**Anything else:** add `include/` and `src/` to your build. The library needs
C++11 and builds without exceptions or RTTI.

## Examples

| Example | What it shows |
|---|---|
| [examples/basic_communication.cpp](examples/basic_communication.cpp) | Two nodes on the link layer |
| [examples/peer_messaging_example.cpp](examples/peer_messaging_example.cpp) | Discovery and messaging by name |
| [examples/encryption_example.cpp](examples/encryption_example.cpp) | The encrypted link |
| [examples/esp32_platformio](examples/esp32_platformio/) | A complete ESP32-S3 + SX1262 application with the radio driver |
| [examples/esp32_bench_test](examples/esp32_bench_test/) | The firmware used for the hardware test: pings, counters, serial commands |

## Building and testing

```bash
cmake -S . -B build && cmake --build build -j
./build/tests/test_all                 # unit tests and simulated-radio scenarios
./build/tests/test_all "[.stress]"     # seed sweeps: each scenario with 200 random seeds
python3 validate_encryption.py         # crypto cross-check (needs the `cryptography` package)
```

The cryptography is self-contained (AES-128, SHA-256, HMAC, PBKDF2) and is
checked against the standard test vectors and against Python's
`cryptography` package; see [CROSS_VALIDATION.md](CROSS_VALIDATION.md) and
[ENCRYPTION_REPORT.md](ENCRYPTION_REPORT.md).

## Things to know

- **Call `processMessages()` with long timeouts and do not sleep in between.**
  The library can only hear packets while your radio is receiving. See the
  receive contract in `IRadio.h`; a driver that keeps the radio in continuous
  receive (like `examples/esp32_platformio/src/SemtechRadio.cpp`) loses the fewest packets.
- **Payload limits:** 246 bytes on a plain link, 207 bytes through
  `EncryptedLoRaLink`, and 4 bytes less for `PeerMessenger` text. Oversized
  sends return `false`; nothing is truncated.
- **Slow settings need long timeouts.** At SF11/SF12 one frame takes around a
  second. If your radio driver implements `timeOnAirMs()` the link adapts its
  ACK timeout automatically; otherwise call `setAckTimeoutMs()`.
- **A shared channel has limited capacity.** Nodes transmit without listening
  first, so keep the total airtime low (a few percent); at 20% load about a
  third of all frames collide.
- **Encryption limits:** no replay protection and no forward secrecy; headers
  (who talks to whom) are not encrypted. Details in `EncryptedLoRaLink.h`.
- **All nodes must run the same major version.** The 2.0 frame format and
  encryption are not compatible with 1.0.

## License

MIT. See [LICENSE.md](LICENSE.md).
