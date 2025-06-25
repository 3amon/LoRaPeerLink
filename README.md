# LoRaPeerLink Library

**LoRaPeerLink** is a comprehensive, standalone LoRa peer-to-peer communication library for microcontrollers. It provides multiple link layer implementations, hardware abstraction, encryption support, and device discovery protocols for building robust LoRa mesh networks without LoRaWAN overhead.

## 🚀 Key Features

- **Drop-in LoRa Communication**: Simple API for reliable peer-to-peer LoRa messaging without LoRaWAN complexity
- **Universal Hardware Support**: Works with any LoRa radio chip through a clean hardware abstraction layer
- **Intelligent Collision Avoidance**: Automatic backoff algorithms prevent message collisions in multi-node networks
- **Built-in Security**: AES encryption with integrity checking for secure communications
- **Automatic Device Discovery**: Find and connect to nearby devices automatically with the RollCall protocol
- **Multi-Platform Ready**: Runs on ESP32, Arduino, STM32, Raspberry Pi Pico, and any microcontroller with SPI
- **Production Ready**: Battle-tested with comprehensive test suite covering real-world scenarios

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
#include "SemtechRadio.h"
#include "LoraBasicLink.h"

// Your timing functions (platform-specific)
uint32_t get_time_ms() { return millis(); }
void sleep_ms(uint32_t ms) { delay(ms); }

// Create radio for 915 MHz (use 868 MHz in Europe)
SemtechRadio radio(915000000);
LoRaBasicLink link(&radio, get_time_ms, sleep_ms);

void setup() {
    Serial.begin(115200);
    
    // Initialize the radio
    if (!radio.begin()) {
        Serial.println("Radio failed to start!");
        return;
    }
    
    // Set your device ID (1-65534)
    link.setLocalId(100);
    Serial.println("LoRa device ready!");
}

void loop() {
    // Send a message every 10 seconds
    static uint32_t lastSend = 0;
    if (millis() - lastSend > 10000) {
        String message = "Hello from device 100!";
        link.sendPacket(100, 0xFFFF, (uint8_t*)message.c_str(), message.length(), false);
        Serial.println("Message sent");
        lastSend = millis();
    }
    
    // Check for incoming messages
    uint16_t fromId;
    uint8_t buffer[255];
    int len = link.receivePacket(&fromId, buffer, sizeof(buffer));
    if (len > 0) {
        buffer[len] = 0; // Null terminate
        Serial.printf("Received from %d: %s\n", fromId, (char*)buffer);
    }
    
    delay(100);
}
```

### What You Need
- **Microcontroller**: ESP32, Arduino, STM32, Raspberry Pi Pico, etc.
- **LoRa Module**: Any Semtech chip (SX1276, SX1278, etc.) or development board
- **Connections**: Just 4 wires (power, ground, and 2 SPI pins)

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

### Automatic Device Discovery

Use RollCall to automatically find and connect to other devices:

```cpp
#include "RollCall.h"

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
        // Send sensor data to gateway
        String data = "temperature:25.3";
        link.sendPacket(discovery.getLocalId(), gatewayId, 
                       (uint8_t*)data.c_str(), data.length(), true);
    }
    
    delay(5000);
}
```

### Secure Communication

Protect your messages with AES encryption:

```cpp
#include "EncryptedLoRaLink.h"

const uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                         0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
EncryptedLoRaLink secureLink(&radio, key, get_time_ms, sleep_ms);

void setup() {
    radio.begin();
    secureLink.setLocalId(1);
}

void loop() {
    // All messages are automatically encrypted/decrypted
    const char* secret = "Top secret data";
    secureLink.sendPacket(1, 2, (uint8_t*)secret, strlen(secret), false);
    
    // Receive and decrypt
    uint16_t fromId;
    uint8_t buffer[100];
    int len = secureLink.receivePacket(&fromId, buffer, sizeof(buffer));
    if (len > 0) {
        buffer[len] = 0;
        Serial.printf("Secure message from %d: %s\n", fromId, (char*)buffer);
    }
    
    delay(1000);
}
```

### Collision Avoidance for Multi-Node Networks

Use backoff algorithm when multiple devices share the same frequency:

```cpp
#include "LoraBackoffLink.h"

LoRaBackoffLink smartLink(&radio, get_time_ms, sleep_ms);

void setup() {
    radio.begin();
    smartLink.setLocalId(1);
}

void loop() {
    // Automatically handles collisions and retries
    const char* message = "Sensor reading: 42";
    smartLink.sendPacket(1, 2, (uint8_t*)message, strlen(message), true);
    
    delay(10000);
}
```

---

## 🔧 Supported Hardware

### Semtech Radio Chips
- **SX127x series**: SX1276, SX1277, SX1278, SX1279
- **SX126x series**: SX1261, SX1262 (with appropriate driver modifications)

### Development Boards
- **Heltec WiFi LoRa 32** (V2, V3)
- **TTGO LoRa32**
- **Adafruit RFM95W** + Arduino/ESP32
- **Any ESP32/Arduino + SX127x module**

### Microcontroller Platforms
- **ESP32** (Arduino, ESP-IDF)
- **Arduino** (Uno, Mega, etc. with external LoRa module)
- **STM32** 
- **Raspberry Pi Pico**
- Any platform with SPI support

---

## 🔧 Custom Radio Implementation

Need to use a different LoRa chip? Implement the `IRadio` interface:

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
    
    int receive(uint8_t* buffer, size_t maxLen) override {
        // Check for incoming data, return length or 0 if none
        return 0;
    }
    
    bool available() override {
        // Return true if data is ready to receive
        return false;
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

### Popular LoRa Modules

| Module | Chip | Frequency | Notes |
|--------|------|-----------|-------|
| Heltec WiFi LoRa 32 | SX1276 | 868/915 MHz | Built-in ESP32, OLED display |
| TTGO LoRa32 | SX1276 | 868/915 MHz | ESP32 with LoRa and GPS |
| Adafruit RFM95W | SX1276 | 868/915 MHz | Breadboard-friendly |
| RAK3172 | SX1262 | 868/915 MHz | Low power, smaller size |

---

## 📖 Real-World Examples

### 🏡 Smart Home Sensor Network
- **[ESP32 Complete Example](examples/esp32_platformio/)**: IoT device with OLED display that can send sensor data and receive commands
- **Use Case**: Temperature sensors reporting to a central gateway, smart switches, garden monitoring

### 🚀 Quick Prototypes  
- **[Basic Communication](examples/basic_communication.cpp)**: Simple two-way messaging between devices
- **[Encrypted Messages](examples/encryption_example.cpp)**: Secure communication for sensitive data
- **Use Case**: Rapid prototyping, learning the library, simple point-to-point links

### 💡 Project Ideas
- **Weather Station Network**: Multiple sensors reporting to a central display
- **Farm Monitoring**: Soil moisture, temperature, and livestock tracking
- **Emergency Communication**: Backup communication when internet/cell towers fail
- **Remote Control**: Control devices in remote locations without WiFi
- **Asset Tracking**: Track equipment, vehicles, or livestock over large areas

### 📚 Additional Resources
- **[PlatformIO Setup Guide](PLATFORMIO_USAGE.md)**: Detailed installation and configuration
- **[API Reference](INTERFACE_USAGE.md)**: Complete function documentation
- **Community Examples**: Check GitHub Issues and Discussions for user-contributed projects

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

**Start building long-range, low-power communication projects today!** Perfect for IoT sensor networks, remote monitoring, emergency communication, agricultural systems, and any project where devices need to talk directly without internet or cell service.

## ⭐ Why Choose LoRaPeerLink?

✅ **Simple**: Start communicating in 5 minutes with minimal code  
✅ **Flexible**: Works with any LoRa hardware through clean abstractions  
✅ **Reliable**: Battle-tested with comprehensive automated testing  
✅ **Secure**: Built-in AES encryption when you need it  
✅ **Smart**: Automatic collision avoidance for multi-device networks  
✅ **Documented**: Clear examples and real-world project templates

