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

2. **LoRaPeerLink Library**: 
   - The complete LoRaPeerLink library is included in the `lib/LoRaPeerLink/` directory
   - No external installation or configuration required - the example is self-contained

3. **Open Project**: Open this directory in PlatformIO IDE or VS Code with PlatformIO extension

4. **Configure Board**: Modify `platformio.ini` if using a different board

5. **Build and Upload**: 
   ```bash
   pio run --target upload
   ```

## Code Structure

- `src/main.cpp`: Main application logic demonstrating LoRaPeerLink usage
- `src/ScreenHandler.cpp`: OLED display management
- `include/ScreenHandler.h`: Display interface definitions
- `include/LoraHandler.h`: High-level LoRa communication wrapper (legacy)
- `lib/LoRaPeerLink/`: Complete LoRaPeerLink library (headers and source files)
- `lib/`: Hardware-specific libraries for display and LoRa hardware

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

## Customization

- Modify frequency in `SemtechRadio` constructor for your region
- Adjust display layout in `ScreenHandler` class
- Add encryption by using `EncryptedLoRaLink` instead of `LoRaBasicLink`
- Implement collision avoidance by using `LoRaBackoffLink`

## Notes

- This example uses the legacy file structure for demonstration
- The `LoraHandler.h` class is a higher-level wrapper that may be simplified in future versions
- For new projects, consider using the core LoRaPeerLink classes directly as shown in the main loop