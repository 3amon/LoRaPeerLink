# LoRaPeerLink Encryption Layer

`EncryptedLoRaLink` wraps any `ILoRaLink` and encrypts and authenticates the
payloads that pass through it. Everyone who knows the network name and the
password can read and send; nobody else can.

> Version 1.0 of this class did not provide real encryption (see
> [CHANGELOG.md](CHANGELOG.md)). Version 2.0 is a complete replacement and is
> not compatible with it.

## Scheme

```
keys  = PBKDF2-HMAC-SHA256(password, salt = network name, iterations, 32 bytes)
        encryption key = keys[0..15], MAC key = keys[16..31]
C     = AES-128-CBC(encryption key, IV, PKCS#7(plaintext))
tag   = HMAC-SHA256(MAC key, source ID (2 bytes, big-endian) || IV || C), first 8 bytes
wire  = IV (16 bytes) || C (16 bytes or more) || tag (8 bytes)
```

- **Encrypt-then-MAC.** The receiver checks the tag, in constant time, before
  it decrypts anything. A packet that fails is dropped silently and counted
  in `rejectedPackets()`.
- **The tag covers the sender's ID**, so a packet cannot be replayed under
  another node's address.
- **IVs** are produced by running the platform's random bytes through HMAC
  together with a counter. Even a random source that returns the same bytes
  every time cannot make an IV repeat within one power cycle.
- **Headers stay in the clear**: source, destination, sequence number and
  flags are needed for filtering and acknowledgments.

## Sizes

| | Bytes |
|---|---|
| Overhead per packet | 25 to 40 (16 IV, 8 tag, 1 to 16 padding) |
| Largest plaintext | 207 (on a standard link with 246 byte payloads) |
| Largest `PeerMessenger` text over an encrypted link | 203 |

## Cost

Key derivation runs once, in the constructor. 4096 PBKDF2 iterations are
about 16,000 SHA-256 block operations: well under a second on an ESP32, but
noticeable. Every node on a network must use the same iteration count.
Per packet the cost is one HMAC and a few AES blocks.

## What it does not do

- **No replay protection.** A recorded packet can be sent again and will be
  accepted (the link layer suppresses duplicates only for a few seconds). If
  that matters, put a counter or a timestamp in the payload.
- **No forward secrecy and no per-node keys.** One shared password; anyone
  who learns it can read past and future traffic and impersonate any node.
- **No traffic confidentiality.** Addresses and packet lengths are visible.
- **An acknowledgment is not authenticated.** The link layer acknowledges a
  frame before this layer has verified it, so "acknowledged" means "received
  by that node's radio", not "accepted as authentic".
- The AES implementation is table based and not hardened against timing side
  channels on shared hardware.

## Implementation and verification

The primitives are in `src/LplCrypto.cpp` (AES-128, SHA-256, HMAC-SHA256,
PBKDF2), written in portable C++ with no dependencies so the library builds
for any microcontroller.

- `tests/test_crypto.cpp`: FIPS 197, SP 800-38A, FIPS 180-4, RFC 4231 and
  RFC 7914 test vectors.
- `tests/test_encrypted_link.cpp`: packet format, every single-bit flip
  rejected, wrong credentials, 200,000 random packets, IV uniqueness, and
  end-to-end tests on the simulated radio.
- `validate_encryption.py`: 1,266 random comparisons against Python's
  `hashlib` and `cryptography` package; see
  [CROSS_VALIDATION.md](CROSS_VALIDATION.md).

## Usage

```cpp
LoRaBackoffLink link(&radio, get_time_ms, sleep_ms);
EncryptedLoRaLink secure(&link, "my-network", "my-password",
                         EncryptedLoRaLink::DEFAULT_KEY_ITERATIONS,
                         get_time_ms,           // optional: lets receivePacket() use its full timeout
                         my_random_bytes);      // optional: hardware random source

RollCall rollCall(&secure, "sensor-01", get_time_ms, sleep_ms, random_16);
PeerMessenger messenger(&rollCall);
```
