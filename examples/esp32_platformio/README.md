# ESP32 PlatformIO Example

This example demonstrates how to use the LoRaPeerLink library with an ESP32-based LoRa development board using PlatformIO.

## Hardware Requirements

- Heltec WiFi LoRa 32 V3 (or compatible ESP32 + LoRa board)
- Built-in OLED display (SSD1306)

## Features Demonstrated

- Basic LoRa peer-to-peer communication
- Button-triggered message broadcasting
- OLED display integration for status and message display
- Interrupt-based user input handling
- Device discovery using RollCall protocol

## Setup Instructions

1. **Install PlatformIO**: Follow the [PlatformIO installation guide](https://platformio.org/install)

2. **Copy to New Repository** (Recommended):
   ```bash
   # Create a new repository for your ESP32 project
   git clone https://github.com/yourusername/your-esp32-project.git
   cd your-esp32-project
   
   # Copy the ESP32 example files
   cp -r /path/to/LoRaPeerLink/examples/esp32_platformio/* .
   ```

3. **LoRaPeerLink Library**: 
   - The library is automatically downloaded from GitHub via PlatformIO's dependency manager
   - See `platformio.ini` for the Git dependency configuration

4. **Build and Upload**: 
   ```bash
   pio run --target upload
   ```

## Code Structure

- `src/main.cpp`: Main application logic demonstrating LoRaPeerLink usage
- `src/ScreenHandler.cpp`: OLED display management
- `include/ScreenHandler.h`: Display interface definitions
- `include/LoraHandler.h`: High-level LoRa communication wrapper (legacy)
- `lib/`: Hardware-specific libraries for display and LoRa hardware
- `platformio.ini`: Project configuration with LoRaPeerLink Git dependency

## Usage

1. Upload the firmware to multiple ESP32 devices
2. Press the GPIO0 button on any device to broadcast a message
3. Watch the OLED display for incoming messages and status updates
4. Monitor serial output for debugging information

## Library Integration

This example shows how to use the LoRaPeerLink library components:

```cpp
#include <SemtechRadio.h>
#include <LoraBasicLink.h>
#include <RollCall.h>

// Create radio instance for 915 MHz
SemtechRadio radio(915000000);

// Create basic link layer
LoRaBasicLink basic_link(&radio, get_time_ms, sleep_ms);

// Create roll call for device discovery
RollCall roll_call(&basic_link, "lora-simp", get_time_ms, sleep_ms, nullptr, RollCall::consoleLog);
```

## Standalone Repository Setup

To create a completely independent project:

1. Create a new GitHub repository
2. Copy this entire `esp32_platformio` directory to your new repository
3. The `platformio.ini` is already configured to pull LoRaPeerLink from GitHub
4. Add your own README linking back to the main [LoRaPeerLink repository](https://github.com/3amon/LoRaPeerLink)

## Customization

- Modify frequency in `SemtechRadio` constructor for your region
- Adjust display layout in `ScreenHandler` class
- Add encryption by using `EncryptedLoRaLink` instead of `LoRaBasicLink`
- Implement collision avoidance by using `LoRaBackoffLink`

## Notes

- This example uses Git dependency management for the LoRaPeerLink library
- The library is automatically downloaded and managed by PlatformIO
- For updates to LoRaPeerLink, simply rebuild your project to get the latest version