#!/bin/bash

# LoRaPeerLink Library Export Script
# Creates a PlatformIO-friendly library package

set -e

echo "Creating LoRaPeerLink PlatformIO Library Export..."

# Create export directory
EXPORT_DIR="LoRaPeerLink-Export"
rm -rf "$EXPORT_DIR"
mkdir -p "$EXPORT_DIR/src"

echo "Copying library files..."

# Copy all header files to src/
cp include/*.h "$EXPORT_DIR/src/"

# Copy all source files to src/
cp src/*.cpp "$EXPORT_DIR/src/"

# Copy library.properties for Arduino IDE compatibility
cp library.properties "$EXPORT_DIR/"

# Copy library.json for PlatformIO compatibility  
cp library.json "$EXPORT_DIR/"

# Copy README
cp README.md "$EXPORT_DIR/"

# Copy examples directory
cp -r examples "$EXPORT_DIR/"

echo "Library export structure:"
find "$EXPORT_DIR" -type f | sort

echo ""
echo "Export complete! Directory: $EXPORT_DIR"
echo ""
echo "Files in src/:"
ls -la "$EXPORT_DIR/src/"

echo ""
echo "To use this library in PlatformIO:"
echo "1. Copy the $EXPORT_DIR directory to your project's lib/ folder"
echo "2. Or reference it in platformio.ini lib_deps:"
echo "   lib_deps = file://<path-to-$EXPORT_DIR>"
echo "3. Or publish to PlatformIO registry and reference as:"
echo "   lib_deps = LoRaPeerLink"