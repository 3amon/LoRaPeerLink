# Python-C++ Cross-Validation

This document describes the cross-validation system that ensures compatibility between the Python and C++ encryption implementations in LoRaPeerLink.

## Overview

The validation system directly tests the C++ EncryptedLoRaLink implementation against the Python encryption validation script to ensure both produce compatible results.

## Components

### C++ CLI Tool (`encryption_cli_tool`)

A command-line interface to the C++ EncryptedLoRaLink class that provides:

- **encrypt**: Encrypt hex data using C++ implementation
- **decrypt**: Decrypt hex data using C++ implementation  
- **test**: Round-trip test (encrypt then decrypt, verify integrity)

```bash
# Examples
./build/tests/encryption_cli_tool encrypt TestNetwork Password123 48656c6c6f
./build/tests/encryption_cli_tool decrypt TestNetwork Password123 <encrypted_hex>
./build/tests/encryption_cli_tool test TestNetwork Password123 48656c6c6f
```

### Python Cross-Validation

The `validate_encryption.py` script now includes a `test_python_cpp_cross_validation()` function that:

1. Calls the C++ CLI tool to perform round-trip tests
2. Performs equivalent round-trip tests in Python
3. Verifies both implementations work correctly with the same test data
4. Compares key derivation between implementations

## CI Integration

The GitHub Actions workflow now includes both:

1. **C++ Tests**: `./build/tests/test_all`
2. **Python-C++ Cross-Validation**: `python3 validate_encryption.py`

This ensures every commit is validated for compatibility between implementations.

## Running Tests Locally

```bash
# Build the project
mkdir build && cd build
cmake ..
make

# Run C++ tests
./build/tests/test_all

# Run cross-validation tests
python3 validate_encryption.py
```

## What Gets Validated

- **Round-trip compatibility**: Data encrypted in C++ can be decrypted correctly
- **Round-trip compatibility**: Data encrypted in Python can be decrypted correctly  
- **Key derivation consistency**: Both implementations derive the same keys from network name and password
- **Binary data handling**: Both implementations handle binary data correctly
- **Edge cases**: Empty data, large data, special characters

## Benefits

- **Direct validation**: No longer relies on assumed compatibility
- **Regression detection**: Catches changes that break cross-compatibility
- **CI enforcement**: Prevents merging incompatible implementations
- **Real-world testing**: Uses actual C++ implementation, not just reimplementation