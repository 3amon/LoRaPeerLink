# LoRaPeerLink Library

**LoRaPeerLink** is a comprehensive, standalone LoRa peer-to-peer communication library for microcontrollers. It provides link layer implementations with acknowledgments and retries, hardware abstraction, authenticated encryption, and a device discovery protocol for building LoRa networks of nodes that talk directly to each other, without LoRaWAN overhead. (Nodes must be in radio range of each other: the library does not route or relay.)

## 🚀 Key Features

- **Intuitive Messaging API**: Send messages by device name or ID with automatic discovery
- **Hardware Agnostic**: Works with any LoRa radio through the IRadio interface
- **Multi-Layer Architecture**: Use high-level messaging or low-level link protocols as needed
- **Automatic Device Discovery**: Find and connect to devices by name with RollCall protocol
- **Collision Handling**: Acknowledgments, retransmission with exponential backoff, and duplicate suppression
- **Built-in Security**: AES-128 encryption with HMAC-SHA256 authentication, keys derived from a network password (PBKDF2)
- **Flexible Link Protocols**: Choose from basic, backoff, or encrypted link implementations
- **Tested Against a Radio Simulator**: 190+ test cases, most of them on a simulated half-duplex LoRa channel with real time-on-air, collisions and packet loss (see [TEST_PLAN.md](TEST_PLAN.md))

---

## 🚀 Getting Started

### What You'll Build
With LoRaPeerLink, you can create devices that talk directly to each other over long distances without needing internet, WiFi, or cell towers. Perfect for:
- **IoT sensor networks** that report data to a central gateway
- **Remote monitoring systems** for agriculture, weather, or equipment
- **Mesh networks** where devices relay messages to extend range
- **Emergency communication** systems that work when other networks fail

### Basic Concept
1. **Connect** a LoRa radio module to your microcontroller
2. **Initialize** the LoRaPeerLink library with your radio
3. **Send** messages to other devices using simple function calls
4. **Receive** messages automatically in your main loop

### 5-Minute Quick Start

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

### What You Need
- **Microcontroller**: ESP32, Arduino, STM32, Raspberry Pi Pico, etc.
- **LoRa Module**: Any compatible LoRa radio module
- **Radio Implementation**: You'll need to implement the `IRadio` interface for your specific hardware
- **Connections**: Typically 4-6 wires depending on your radio module

---

## 🏗️ Architecture Overview

LoRaPeerLink uses a clean layered architecture that separates concerns and makes the library flexible and extensible:

```
┌─────────────────────────────────────────────────────────┐
│                    Your Application                     │
├─────────────────────────────────────────────────────────┤
│  PeerMessenger (High-level messaging interface)        │
│  • Send/receive by name or ID                          │
│  • Automatic message queuing                           │
│  • Protocol separation                                 │
├─────────────────────────────────────────────────────────┤
│  RollCall (Device discovery and name resolution)       │
│  • Automatic node discovery                            │
│  • Name-to-ID mapping                                  │
│  • Network topology awareness                          │
├─────────────────────────────────────────────────────────┤
│  ILoRaLink (Link layer abstraction)                    │
│  • LoRaBasicLink: Simple point-to-point                │
│  • LoRaBackoffLink: Collision avoidance                │
│  • EncryptedLoRaLink: Secure communication             │
├─────────────────────────────────────────────────────────┤
│  IRadio (Hardware abstraction layer)                   │
│  • Your radio implementation (SemtechRadio, etc.)      │
│  • Handles actual radio communication                  │
└─────────────────────────────────────────────────────────┘
```

**Key Benefits:**
- **Modularity**: Use only the layers you need
- **Flexibility**: Swap implementations at any layer
- **Extensibility**: Add new protocols or radio support easily
- **Testability**: Mock any layer for testing
- **Hardware Independence**: Works with any radio through IRadio interface

---

## 🛠️ Installation

### For PlatformIO Users

**Option 1: Download Latest Release** (Recommended)
1. Go to [GitHub Actions](https://github.com/3amon/LoRaPeerLink/actions) 
2. Download the `LoRaPeerLink-PlatformIO-Library` artifact
3. Extract to your project's `lib/LoRaPeerLink/` folder

**Option 2: Direct Git Reference**
```ini
[env:your_board]
lib_deps = https://github.com/3amon/LoRaPeerLink.git
```

### For Arduino IDE Users

1. Download this repository as ZIP
2. Extract to your Arduino `libraries/` folder
3. Restart Arduino IDE
4. Include: `#include <LoraBasicLink.h>`

### For Other Platforms

Copy the `include/` and `src/` folders to your project and add to your build system.

---

## 🎯 Advanced Usage

### High-Level Messaging with PeerMessenger

The recommended way to use LoRaPeerLink is through the PeerMessenger interface, which provides automatic discovery and intuitive messaging:

```cpp
#include "PeerMessenger.h"
#include "RollCall.h"
#include "LoraBasicLink.h"

// Setup your radio implementation and link layer
MyRadio radio;
LoRaBasicLink link(&radio, get_time_ms, sleep_ms);
RollCall rollCall(&link, "sensor-01", get_time_ms, sleep_ms);
PeerMessenger messenger(&rollCall);

void setup() {
    radio.begin();
    rollCall.begin();
    messenger.begin();
}

void loop() {
    messenger.processMessages();
    
    // Send to specific device by name
    messenger.sendMessage("gateway", "Temperature: 25.3°C");
    
    // Broadcast to all devices
    messenger.broadcastMessage("System startup complete");
    
    // Handle incoming messages
    if (messenger.hasMessage()) {
        UserMessage msg = messenger.receiveMessage();
        Serial.printf("From %s: %s\n", msg.srcName.c_str(), msg.content.c_str());
    }
}
```

### Device Discovery with RollCall

Use RollCall when you need device discovery but want more control over messaging:

```cpp
#include "RollCall.h"
#include "LoraBasicLink.h"

LoRaBasicLink link(&radio, get_time_ms, sleep_ms);
RollCall discovery(&link, "sensor-01", get_time_ms, sleep_ms);

void setup() {
    radio.begin();
    discovery.begin();  // Announces this device to the network
}

void loop() {
    discovery.processMessages(100);  // Handle discovery protocol
    
    // Find a specific device
    uint16_t gatewayId = discovery.whoIs("gateway");
    if (gatewayId != 0) {
        // Send sensor data to gateway using link layer directly
        String data = "temperature:25.3";
        link.sendPacket(discovery.getNodeId(), gatewayId, 
                       (uint8_t*)data.c_str(), data.length(), true);
    }
}
```

### Direct Link Layer Usage

For maximum control, use the link layer directly:

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

---

## 🔧 Implementing Radio Support

LoRaPeerLink doesn't directly support specific hardware. Instead, it provides the `IRadio` interface that you implement for your radio hardware:

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

### Radio Implementation Examples

For real hardware, you'll typically implement IRadio using existing libraries:

- **ESP32 with SX127x**: Use RadioLib or LoRa library with IRadio wrapper
- **Arduino with RFM95W**: Use RadioHead or similar library with IRadio wrapper  
- **STM32**: Use HAL or LL drivers with IRadio wrapper
- **Raspberry Pi Pico**: Use SPI library with IRadio wrapper

**Example Projects:**
- Check `examples/esp32_platformio/` for a complete ESP32 implementation
- See existing radio wrappers in the community for common platforms

**Popular LoRa Modules:**
- Heltec WiFi LoRa 32 (SX1276)
- TTGO LoRa32 (SX1276) 
- Adafruit RFM95W (SX1276)
- RAK3172 (SX1262)
- Any module with SX127x or SX126x chips

---

## 📖 Real-World Examples

### 🏡 Complete ESP32 Smart Sensor
- **[ESP32 PlatformIO Example](examples/esp32_platformio/)**: Complete IoT device with OLED display
- **Features**: PeerMessenger integration, sensor readings, command handling
- **Use Case**: Temperature sensors, smart switches, garden monitoring

### 🚀 High-Level Messaging  
- **[PeerMessenger Example](examples/peer_messaging_example.cpp)**: Demonstrates the complete messaging stack
- **Features**: Auto-discovery, name-based messaging, broadcast support
- **Use Case**: User-friendly device communication, rapid prototyping

### 🔗 Link Layer Examples
- **[Basic Communication](examples/basic_communication.cpp)**: Simple point-to-point messaging
- **[Encrypted Messages](examples/encryption_example.cpp)**: Secure communication demo
- **Use Case**: Direct device control, security-critical applications

### 💡 Project Ideas
- **Smart Farm Network**: Soil sensors reporting to irrigation controller by name
- **Emergency Mesh**: Devices automatically discover and relay emergency messages
- **Asset Tracking**: Equipment reports location to "tracker-gateway" automatically
- **Home Automation**: Switches send commands to "lighting-controller" by name

### 📚 Getting Started Resources
- **[PlatformIO Setup Guide](PLATFORMIO_USAGE.md)**: Complete installation and configuration
- **[Interface Documentation](INTERFACE_USAGE.md)**: Detailed API reference
- **[Architecture Guide](SEPARATION_SUMMARY.md)**: Understanding the layer separation

---

## ✅ Testing and Project Status

```bash
cmake -S . -B build && cmake --build build -j
./build/tests/test_all                 # unit tests and simulated-radio scenarios
./build/tests/test_all "[.stress]"     # seed sweeps: each scenario with 200 random seeds
python3 validate_encryption.py         # crypto cross-check (pip install cryptography)
```

Most tests run the real library code on a simulated radio channel
(`tests/sim/`): virtual time, LoRa time-on-air, half-duplex radios, collisions,
random loss, and both kinds of radio driver (one that only listens inside
`receive()`, one that listens continuously). See **[TEST_PLAN.md](TEST_PLAN.md)**
for what is covered and what is not, and **[CHANGELOG.md](CHANGELOG.md)** for
the bugs version 2.0 fixed.

**Hardware status:** the library and the ESP32 example compile for the Heltec
WiFi LoRa 32 V3 (ESP32-S3 + SX1262). Version 2.0 has not yet been validated on
real radios; `TEST_PLAN.md` lists the on-air checks still to do.

### Things to know

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

---

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Add tests for new functionality
4. Ensure all tests pass: `make test`
5. Submit a pull request

---

## 📄 License

This library is released under the MIT License. See `LICENSE` file for details.

---

## 🆘 Support

- **Issues**: Report bugs and feature requests via GitHub Issues
- **Questions**: Ask in GitHub Discussions
- **Documentation**: See `examples/` directory and code comments
- **Community**: Share your projects and get help from other users

---

**Build powerful LoRa networks with intuitive, name-based messaging!** Perfect for IoT sensors, remote monitoring, emergency communication, and any project where devices need reliable, long-range communication without infrastructure.

## ⭐ Why Choose LoRaPeerLink?

✅ **Intuitive**: Send messages by device name, not complex IDs  
✅ **Flexible**: Use high-level messaging or low-level protocols as needed  
✅ **Hardware Agnostic**: Implement once, run on any LoRa radio  
✅ **Tested**: Protocol behaviour verified on a simulated half-duplex LoRa channel; cryptography verified against standard test vectors and an independent implementation  
✅ **Layered Design**: Clean architecture that separates concerns

