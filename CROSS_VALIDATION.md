# Python / C++ Cross-Validation

The library ships its own AES-128, SHA-256, HMAC and PBKDF2 so that it builds
on a microcontroller without dependencies. Home-made cryptography has to be
checked against something independent, so `validate_encryption.py` compares
the C++ code with Python's `hashlib` and the `cryptography` package.

## How it works

`tests/encryption_cli_tool.cpp` is a small command line front end to the
library's crypto (built as `build/tests/encryption_cli_tool`):

```
encryption_cli_tool keys   <network> <password> <iterations>
encryption_cli_tool seal   <network> <password> <iterations> <srcId> <iv hex> <plaintext hex>
encryption_cli_tool open   <network> <password> <iterations> <srcId> <wire hex>
encryption_cli_tool sha256 <data hex>
encryption_cli_tool hmac   <key hex> <data hex>
encryption_cli_tool aes    <key hex> <block hex>
encryption_cli_tool pbkdf2 <password hex> <salt hex> <iterations> <length>
```

`validate_encryption.py` contains its own implementation of the packet format
and, with random inputs, checks that:

- SHA-256, HMAC-SHA256, AES-128 and PBKDF2 give identical output;
- both sides derive the same keys from a network name and password;
- both sides produce byte-for-byte identical packets for the same IV;
- packets made by Python open in C++ and return the plaintext;
- a flipped bit, another sender ID or another password is rejected;
- a packet with a valid tag but invalid padding is rejected.

## Running it

```bash
cmake -S . -B build && cmake --build build -j
python3 validate_encryption.py              # needs: pip install cryptography
# or, without installing anything:
uv run --with cryptography validate_encryption.py
```

Expected output ends with `1266 checks passed, 0 failed`. CI runs it after
the C++ tests.
