# Guide: Creating a Separate Repository for ESP32 Example

This guide shows how to move this ESP32 example to its own repository with proper LoRaPeerLink dependency management.

## Quick Setup

1. **Create New Repository**:
   ```bash
   # On GitHub, create a new repository (e.g., "LoRaPeerLink-ESP32-Example")
   git clone https://github.com/yourusername/LoRaPeerLink-ESP32-Example.git
   cd LoRaPeerLink-ESP32-Example
   ```

2. **Copy Example Files**:
   ```bash
   # Copy all files from this esp32_platformio directory
   cp -r /path/to/LoRaPeerLink/examples/esp32_platformio/* .
   ```

3. **Initialize Repository**:
   ```bash
   git add .
   git commit -m "Initial ESP32 PlatformIO example with LoRaPeerLink dependency"
   git push origin main
   ```

## Dependency Configuration

The `platformio.ini` is already configured with the correct Git dependency:

```ini
lib_deps = 
    rweather/Crypto@^0.4.0
    agdl/Base64@^1.0.0
    https://github.com/3amon/LoRaPeerLink.git
```

This will automatically download the latest LoRaPeerLink library when building.

## Repository Structure

Your separate repository will have:

```
LoRaPeerLink-ESP32-Example/
├── src/
│   ├── main.cpp
│   └── ScreenHandler.cpp
├── include/
│   ├── ScreenHandler.h
│   └── LogBundler.hpp
├── lib/
│   ├── DISPLAY/
│   └── LoraWan102/
├── platformio.ini
├── README.md
└── .gitignore
```

## Benefits

✅ **Clean Separation**: Example is completely independent
✅ **Automatic Updates**: PlatformIO pulls latest LoRaPeerLink automatically  
✅ **Template Ready**: Others can clone and use immediately
✅ **No Sync Issues**: No need to manually update copied library files

## Customization for Your Project

1. **Update README.md**: Add your project-specific information
2. **Link Back**: Add reference to main LoRaPeerLink repository
3. **Add Features**: Extend the example for your specific use case
4. **Version Pinning** (optional): Pin to specific LoRaPeerLink version if needed:
   ```ini
   https://github.com/3amon/LoRaPeerLink.git#v1.0.0
   ```

## Testing

After setup, test that everything works:

```bash
cd your-new-repository
pio run          # Should download LoRaPeerLink and build successfully
pio run -t upload # Upload to your ESP32 board
```

This approach eliminates the file duplication issue while maintaining a clean, standalone example project.