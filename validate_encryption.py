#!/usr/bin/env python3
"""
Cross-validate LoRaPeerLink's C++ cryptography against an independent
implementation (Python's hashlib and the "cryptography" package).

The C++ library ships its own AES-128, SHA-256, HMAC and PBKDF2 so that it
builds on a microcontroller without dependencies. This script checks, with
random inputs, that every one of them and the EncryptedLoRaLink packet format
produce exactly the bytes a standard library produces.

Packet format (see include/EncryptedLoRaLink.h):

    keys = PBKDF2-HMAC-SHA256(password, salt=network name, iterations, 32 bytes)
    wire = IV(16) || AES-128-CBC(keys[:16], IV, PKCS7(plaintext))
                  || HMAC-SHA256(keys[16:], srcId(2, big-endian) || IV || ciphertext)[:8]

Usage:
    cmake -S . -B build && cmake --build build
    python3 validate_encryption.py            (needs: pip install cryptography)
    uv run --with cryptography validate_encryption.py
"""

import hashlib
import hmac
import os
import random
import subprocess
import sys

from cryptography.hazmat.primitives import padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

TOOL_CANDIDATES = ["build/tests/encryption_cli_tool", "build/encryption_cli_tool"]
TAG_SIZE = 8


def find_tool():
    here = os.path.dirname(os.path.abspath(__file__))
    for candidate in TOOL_CANDIDATES:
        path = os.path.join(here, candidate)
        if os.path.exists(path):
            return path
    sys.exit("encryption_cli_tool not found; build the project first (cmake -S . -B build && cmake --build build)")


TOOL = find_tool()


def hx(data: bytes) -> str:
    return data.hex() if data else "-"


def cpp(*args) -> str:
    result = subprocess.run([TOOL, *[str(a) for a in args]], capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        raise RuntimeError(f"tool failed: {args}: {result.stderr.strip()}")
    return result.stdout.strip()


def unhx(text: str) -> bytes:
    return b"" if text == "-" else bytes.fromhex(text)


# ---- Independent reference implementation -----------------------------------

def ref_keys(network: str, password: str, iterations: int) -> bytes:
    return hashlib.pbkdf2_hmac("sha256", password.encode(), network.encode(), max(iterations, 1), 32)


def ref_seal(keys: bytes, src_id: int, iv: bytes, plaintext: bytes) -> bytes:
    padder = padding.PKCS7(128).padder()
    padded = padder.update(plaintext) + padder.finalize()
    enc = Cipher(algorithms.AES(keys[:16]), modes.CBC(iv)).encryptor()
    ciphertext = enc.update(padded) + enc.finalize()
    tag = hmac.new(keys[16:], src_id.to_bytes(2, "big") + iv + ciphertext, hashlib.sha256).digest()[:TAG_SIZE]
    return iv + ciphertext + tag


def ref_open(keys: bytes, src_id: int, wire: bytes):
    if len(wire) < 16 + 16 + TAG_SIZE or (len(wire) - 16 - TAG_SIZE) % 16 != 0:
        return None
    iv, ciphertext, tag = wire[:16], wire[16:-TAG_SIZE], wire[-TAG_SIZE:]
    expected = hmac.new(keys[16:], src_id.to_bytes(2, "big") + iv + ciphertext, hashlib.sha256).digest()[:TAG_SIZE]
    if not hmac.compare_digest(tag, expected):
        return None
    dec = Cipher(algorithms.AES(keys[:16]), modes.CBC(iv)).decryptor()
    padded = dec.update(ciphertext) + dec.finalize()
    unpadder = padding.PKCS7(128).unpadder()
    try:
        return unpadder.update(padded) + unpadder.finalize()
    except ValueError:
        return None


# ---- Checks ------------------------------------------------------------------

class Checker:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def check(self, condition: bool, description: str):
        if condition:
            self.passed += 1
        else:
            self.failed += 1
            print(f"  FAIL: {description}")


def check_primitives(c: Checker, rng: random.Random):
    print("Primitives against hashlib / cryptography (random inputs)")
    for _ in range(150):
        data = rng.randbytes(rng.choice([0, 1, 55, 56, 63, 64, 65, 119, 120, 128, rng.randrange(0, 600)]))
        c.check(cpp("sha256", hx(data)) == hashlib.sha256(data).hexdigest(), f"sha256 of {len(data)} bytes")
    for _ in range(150):
        key = rng.randbytes(rng.choice([0, 1, 16, 32, 63, 64, 65, 131]))
        data = rng.randbytes(rng.randrange(0, 300))
        c.check(cpp("hmac", hx(key), hx(data)) == hmac.new(key, data, hashlib.sha256).hexdigest(),
                f"hmac key {len(key)} data {len(data)}")
    for _ in range(150):
        key, block = rng.randbytes(16), rng.randbytes(16)
        enc = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
        c.check(cpp("aes", hx(key), hx(block)) == (enc.update(block) + enc.finalize()).hex(), "aes-128 block")
    for _ in range(60):
        password = rng.randbytes(rng.randrange(0, 90))
        salt = rng.randbytes(rng.randrange(0, 90))
        iterations = rng.choice([1, 2, 3, 10, 100, 1000])
        length = rng.choice([1, 16, 31, 32, 33, 64, 80])
        expected = hashlib.pbkdf2_hmac("sha256", password, salt, iterations, length).hex()
        c.check(cpp("pbkdf2", hx(password), hx(salt), iterations, length) == expected,
                f"pbkdf2 iterations {iterations} length {length}")


def check_packets(c: Checker, rng: random.Random):
    print("EncryptedLoRaLink packets against the reference implementation")
    credentials = [("LoRaNet", "hunter2", 4096), ("camp", "correct horse battery staple", 4096),
                   ("n", "p", 1), ("Ünïcödé network", "pässwörd ✓", 50), ("x" * 60, "y" * 90, 7)]
    for network, password, iterations in credentials:
        keys = ref_keys(network, password, iterations)
        c.check(cpp("keys", network, password, iterations) == keys.hex(), f"key derivation for {network!r}")

        for _ in range(25):
            src = rng.randrange(1, 0xFFFF)
            iv = rng.randbytes(16)
            plaintext = rng.randbytes(rng.choice([0, 1, 15, 16, 17, 31, 32, 100, 207, rng.randrange(0, 208)]))
            wire = ref_seal(keys, src, iv, plaintext)

            # Same bytes from both implementations.
            c.check(cpp("seal", network, password, iterations, src, hx(iv), hx(plaintext)) == wire.hex(),
                    f"seal {len(plaintext)} bytes")
            # Python's packet opens in C++ and gives the plaintext back.
            c.check(cpp("open", network, password, iterations, src, hx(wire)) == "OK " + hx(plaintext),
                    f"open {len(plaintext)} bytes")

            # Tampering is rejected by both implementations.
            position = rng.randrange(len(wire))
            tampered = bytearray(wire)
            tampered[position] ^= 1 << rng.randrange(8)
            tampered = bytes(tampered)
            c.check(ref_open(keys, src, tampered) is None, "reference rejects a tampered packet")
            c.check(cpp("open", network, password, iterations, src, hx(tampered)) == "REJECTED",
                    f"C++ rejects a bit flip at byte {position}")

            # Wrong sender ID, wrong password.
            c.check(cpp("open", network, password, iterations, (src % 0xFFFE) + 1, hx(wire)) == "REJECTED",
                    "C++ rejects a packet attributed to another sender")
            c.check(cpp("open", network, password + "!", iterations, src, hx(wire)) == "REJECTED",
                    "C++ rejects a packet from a network with another password")

    # A packet with a correct tag but invalid padding (cannot be produced by seal()).
    keys = ref_keys("LoRaNet", "hunter2", 4096)
    iv = rng.randbytes(16)
    enc = Cipher(algorithms.AES(keys[:16]), modes.CBC(iv)).encryptor()
    ciphertext = enc.update(bytes(15) + b"\x00") + enc.finalize()
    tag = hmac.new(keys[16:], (7).to_bytes(2, "big") + iv + ciphertext, hashlib.sha256).digest()[:TAG_SIZE]
    c.check(cpp("open", "LoRaNet", "hunter2", 4096, 7, hx(iv + ciphertext + tag)) == "REJECTED",
            "C++ rejects valid tag with invalid padding")


def main():
    rng = random.Random(20260928)
    c = Checker()
    check_primitives(c, rng)
    check_packets(c, rng)
    print(f"\n{c.passed} checks passed, {c.failed} failed")
    sys.exit(1 if c.failed else 0)


if __name__ == "__main__":
    main()
