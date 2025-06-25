# Using LoRaPeerLink in PlatformIO Projects

This document explains how to use the LoRaPeerLink library in your PlatformIO projects.

## Method 1: Download from GitHub Actions (Recommended)

1. Go to the [GitHub Actions page](https://github.com/3amon/LoRaPeerLink/actions) for this repository
2. Click on the latest successful build
3. Download the `LoRaPeerLink-PlatformIO-Library` artifact
4. Extract the downloaded ZIP file to get the `LoRaPeerLink-Export` directory
5. Copy the `LoRaPeerLink-Export` directory to your project's `lib/` folder and rename it to `LoRaPeerLink`

Your project structure should look like:
```
YourProject/
├── src/
│   └── main.cpp
├── lib/
│   └── LoRaPeerLink/          # Copied from LoRaPeerLink-Export
│       ├── src/
│       │   ├── *.h            # All header files
│       │   └── *.cpp          # All source files
│       ├── library.json
│       ├── library.properties
│       └── examples/
├── platformio.ini
└── ...
```

## Method 2: Reference from Local Directory

If you have the library source code locally:

1. Build the export using the provided script:
   ```bash
   cd /path/to/LoRaPeerLink
   ./export_library.sh
   ```

2. Reference it in your `platformio.ini`:
   ```ini
   [env:your_board]
   platform = espressif32
   board = your_board
   framework = arduino
   lib_deps = file:///absolute/path/to/LoRaPeerLink-Export
   ```

## Method 3: Git Submodule (Advanced)

For development and version control:

1. Add the repository as a submodule in your project's `lib/` directory:
   ```bash
   cd your_project
   git submodule add https://github.com/3amon/LoRaPeerLink.git lib/LoRaPeerLink
   ```

2. Update your `platformio.ini`:
   ```ini
   [env:your_board]
   platform = espressif32
   board = your_board
   framework = arduino
   lib_extra_dirs = lib
   ```

## Usage Example

Once installed, include the library in your code:

```cpp
#include <Arduino.h>
#include <LoraBasicLink.h>  // For basic LoRa communication
#include <RollCall.h>       // For device discovery

// Your code here...
```

## Available Components

The library provides several components:

- **LoraBasicLink**: Basic LoRa peer-to-peer communication
- **LoraBackoffLink**: Communication with collision avoidance
- **EncryptedLoRaLink**: Encrypted LoRa communication
- **RollCall**: Device discovery and naming protocol
- **IRadio**: Hardware abstraction interface

## Complete Examples

Check the `examples/` directory in the library for:
- `basic_communication.cpp`: Simple usage example
- `encryption_example.cpp`: Encrypted communication demo
- `esp32_platformio/`: Complete ESP32 application

## Hardware Dependencies

The library requires a LoRa radio module. The examples typically use:
- Semtech SX127x series (SX1276, SX1278, etc.)
- Semtech SX126x series

Make sure to implement the `IRadio` interface for your specific hardware or use one of the provided implementations.

## Troubleshooting

- **Compilation errors**: Ensure all required hardware abstraction layers are implemented
- **Library not found**: Check that the library path is correct in `lib_deps` or the library is in the `lib/` directory
- **Runtime issues**: Verify your radio module wiring and initialization code

For more help, check the [main repository](https://github.com/3amon/LoRaPeerLink) documentation and examples.