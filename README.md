# LoRaPeerLink Library

**LoRaPeerLink** is a comprehensive, standalone LoRa peer-to-peer communication library for microcontrollers. It provides multiple link layer implementations, hardware abstraction, encryption support, and device discovery protocols for building robust LoRa mesh networks without LoRaWAN overhead.

## 🚀 Key Features

- **Hardware Abstraction**: Works with any LoRa radio via the `IRadio` interface
- **Multiple Link Layers**: Basic, backoff-based collision avoidance, and encrypted communication
- **Device Discovery**: Built-in RollCall protocol for automatic node discovery
- **Encryption Support**: AES encryption with integrity checking
- **Platform Agnostic**: Runs on Arduino, PlatformIO, and other microcontroller platforms
- **Comprehensive Testing**: 1000+ test assertions with mock radio simulation
- **Well Documented**: Extensive API documentation and usage examples

---

## 📦 Library Structure

```
LoRaPeerLink/
├── include/                    # Library header files
│   ├── IRadio.h               # Hardware abstraction interface  
│   ├── ILoRaLink.h            # Link layer interface
│   ├── LoraBasicLink.h        # Basic communication implementation
│   ├── LoraBackoffLink.h      # Collision avoidance implementation
│   ├── EncryptedLoRaLink.h    # Encrypted communication layer
│   ├── SemtechRadio.h         # Semtech radio hardware driver
│   └── RollCall.h             # Device discovery protocol
├── src/                       # Library source files
├── examples/                  # Usage examples and demos
│   ├── basic_communication.cpp # Simple usage example
│   ├── encryption_example.cpp  # Encryption layer demo
│   └── esp32_platformio/       # Complete ESP32 application
├── tests/                     # Comprehensive test suite
├── CMakeLists.txt             # Build configuration for testing
└── library.json               # PlatformIO library metadata
```

---

## 🛠️ Installation

### PlatformIO (Recommended)

1. **Install via PlatformIO Registry** (when published):
   ```ini
   [env:your_board]
   lib_deps = LoRaPeerLink
   ```

2. **Install from Source**:
   - Clone or download this repository
   - Copy the entire directory to your PlatformIO `lib/` folder
   - Or use `lib_extra_dirs` in `platformio.ini`

### Arduino IDE

1. Download this repository as a ZIP file
2. Extract to your Arduino `libraries/` folder
3. Restart Arduino IDE
4. Include the library: `#include <LoraBasicLink.h>`

### Manual Integration

Simply copy the `include/` and `src/` directories to your project and add them to your build system.

---

## 🎯 Quick Start

### Basic Communication

```cpp
#include "SemtechRadio.h"
#include "LoraBasicLink.h"

// Timing functions (implement for your platform)
uint32_t get_time_ms() { return millis(); }
void sleep_ms(uint32_t ms) { delay(ms); }

// Create radio and link instances
SemtechRadio radio(915000000);  // 915 MHz
LoRaBasicLink link(&radio, get_time_ms, sleep_ms);

void setup() {
    // Initialize radio
    radio.begin();
    link.setLocalId(1);  // Set unique node ID
}

void loop() {
    // Send message
    const char* message = "Hello World";
    link.sendPacket(1, 2, (uint8_t*)message, strlen(message), false);
    
    // Receive messages
    uint16_t sourceId;
    uint8_t buffer[100];
    int len = link.receivePacket(&sourceId, buffer, 100);
    if (len > 0) {
        // Process received message
        buffer[len] = '\0';
        Serial.printf("From %d: %s\n", sourceId, (char*)buffer);
    }
    
    delay(1000);
}
```

### With Device Discovery

```cpp
#include "RollCall.h"

RollCall rollcall(&link, "MyDevice", get_time_ms, sleep_ms);

void setup() {
    radio.begin();
    rollcall.begin();
}

void loop() {
    rollcall.processMessages(100);  // Handle discovery protocol
    // Your application logic here
}
```

### With Encryption

```cpp
#include "EncryptedLoRaLink.h"

const uint8_t key[16] = {0x00, 0x01, 0x02, ...};  // Your AES key
EncryptedLoRaLink encLink(&radio, key, get_time_ms, sleep_ms);

// Use encLink.sendPacket() and encLink.receivePacket() as normal
// All communication is automatically encrypted/decrypted
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

## 📋 Library Components

### Link Layer Implementations

| Class | Features | Use Case |
|-------|----------|----------|
| `LoRaBasicLink` | Simple send/receive, optional ACK | Basic P2P communication |
| `LoRaBackoffLink` | Collision avoidance, exponential backoff | Networks with multiple nodes |
| `EncryptedLoRaLink` | AES encryption, integrity checking | Secure communication |

### Hardware Abstraction

| Class | Purpose |
|-------|---------|
| `IRadio` | Abstract radio interface |
| `SemtechRadio` | Semtech chipset implementation |
| `MockRadio` | Simulation for testing |

### Protocols

| Class | Purpose |
|-------|---------|
| `RollCall` | Device discovery and presence |

---

## 🧪 Testing

The library includes a comprehensive test suite with 1000+ assertions covering all major functionality.

### Run Tests

```bash
mkdir build && cd build
cmake ..
make
./tests/test_all
```

### Test Coverage
- ✅ Basic send/receive operations
- ✅ Packet framing and CRC validation  
- ✅ Collision avoidance and backoff timing
- ✅ Encryption/decryption and key management
- ✅ Device discovery protocols
- ✅ Error handling and edge cases

---

## 📖 Examples

### Complete Applications
- **[ESP32 PlatformIO Example](examples/esp32_platformio/)**: Full-featured LoRa device with OLED display
- **[Basic Communication](examples/basic_communication.cpp)**: Minimal usage example
- **[Encryption Demo](examples/encryption_example.cpp)**: Secure communication examples

### Key Documentation
- **[Interface Usage Guide](INTERFACE_USAGE.md)**: Detailed API reference
- **[Encryption Report](ENCRYPTION_REPORT.md)**: Security implementation details
- **[Test Summary](TEST_SUMMARY.md)**: Comprehensive testing documentation

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
- **Discussions**: Ask questions in GitHub Discussions
- **Documentation**: Check the `examples/` directory and header file documentation

---

**Perfect for**: IoT sensor networks, mesh communication, remote monitoring, agricultural sensors, environmental monitoring, and any application requiring reliable peer-to-peer LoRa communication.

