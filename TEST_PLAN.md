# LoRaPeerLink Test Plan

This document says what is tested, how, and what still has to be checked on
real radios. It was written for version 2.0, after version 1.0 passed all of
its unit tests but did not work on hardware.

## Why version 1.0 passed its tests and still failed

The original tests used `MockRadio`, a single FIFO shared by every node.
Packets arrived instantly, were never lost, and a sender could read its own
packet back. Time did not exist. None of that is true for a LoRa radio:

| Real radio | What it breaks if the code assumes otherwise |
|---|---|
| A frame takes 40 ms to several seconds to transmit | ACK timeouts, polling windows |
| A node cannot hear anything while it transmits | Two nodes talking to each other |
| A node only hears frames while its receiver is on | Anything that sleeps or polls in short windows |
| Two overlapping frames destroy each other | Retries, simultaneous startup |
| Frames are lost and repeated | Acknowledgments, duplicate delivery |
| Anyone can transmit anything | Parsing of received data |

So the plan has three layers: a radio simulator that has these properties,
scenario tests on top of it, and a short list of checks that only real
hardware can answer.

## 1. The radio simulator (`tests/sim/`)

`SimRadio` implements `IRadio`. Every node's (blocking) program runs in its
own thread under a virtual clock, so a ten minute scenario takes milliseconds
and every run is repeatable.

- **Time on air** from the LoRa modem settings (Semtech AN1200.13). Default
  SF7 / 125 kHz / CR 4/5 / 12 symbol preamble; SF10 to SF12 are used too.
- **Half duplex.** A transmitting node hears nothing.
- **Collisions.** Overlapping frames are lost at every receiver in range.
- **Two receive models**, because the driver decides how the radio listens:
  - *Windowed*: the radio listens only inside `receive()`, and a frame is
    heard only if a single call covers it from preamble to end. This is how
    the original `SemtechRadio` driver behaves, and it is the harsh case.
  - *Continuous*: the radio listens whenever it is not transmitting and keeps
    the last frame. This is how the new example driver behaves.
- **Random loss, topology (who hears whom), and a hook** to drop or corrupt
  individual frames.

The simulator has its own tests (`test_sim_radio.cpp`): time on air against
published values, clock behaviour, both receive models, half duplex,
collisions, hidden nodes, loss, determinism.

Nearly every scenario below runs four times: Basic link and Backoff link,
windowed and continuous radio.

## 2. What is tested

### Link layer (`test_sim_link.cpp`, plus the older unit tests)

| Area | Scenarios |
|---|---|
| Delivery | Unicast and broadcast, with and without ACK; maximum payload (246 bytes) and one byte more; empty payload |
| Acknowledgment | ACK round trip; no answer (retries, then failure); lost ACK; lost data frame; ACK accepted only from the right node, to the right node, with the right sequence number and a valid CRC; broadcast never waits for an ACK |
| Duplicates | A retransmission after a lost ACK is delivered once; 300 identical payloads (sequence number wraps) are all delivered |
| Concurrency | Two nodes sending to each other at once; three nodes exchanging acknowledged messages; four nodes transmitting at the same instant |
| Busy receiver | Frames arriving while a node waits for its own ACK are queued and acknowledged; a full queue never acknowledges what it drops |
| Timeouts | `receivePacket` waits its full timeout through foreign traffic; zero timeout polls; ACK timeout at SF11 with and without a driver that reports time on air |
| Interoperability | Basic and Backoff links exchange acknowledged frames; frames on the air match the documented byte layout |
| Robustness | 2000 random frames per run; buffer too small; null pointers; a clock that never advances |
| Statistics | Counters match a scripted sequence of events |

### RollCall (`test_sim_rollcall.cpp`, `test_rollcall.cpp`)

| Area | Scenarios |
|---|---|
| Discovery | Two and six nodes switched on at the same instant; a late joiner is known within seconds; a node that restarts with a new ID replaces its old entry |
| Collisions | Same name on four nodes (exactly one keeps it); same ID on two nodes (exactly one moves); same name and same ID (resolved by nonce) |
| Queries | `whoIs` / `whereIs` for a node whose announcements were never heard; queries that get no answer end on time |
| Hostile input | 35 malformed messages on the air; 20,000 random messages into the parser; table size limit; invalid names; a stuck random source |
| Timing | Announcements at begin, again within seconds, then every 27 to 33 s, including when the application only calls `PeerMessenger` |

### PeerMessenger (`test_sim_messenger.cpp`, `test_peer_messenger.cpp`)

By name and by ID, acknowledged and broadcast; messages arriving while a name
is being resolved; the "call RollCall, then PeerMessenger" loop used by the
WirelessStick application; over-long messages; binary content; queue limit;
a four node soak with 5% packet loss where every acknowledged message must
arrive exactly once.

### Encryption (`test_crypto.cpp`, `test_encrypted_link.cpp`, `validate_encryption.py`)

| Area | Checks |
|---|---|
| Primitives | AES-128 (FIPS 197, SP 800-38A), SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 4231), PBKDF2 (RFC 7914 vectors), CBC with PKCS#7 for every length |
| Independent implementation | 1,266 random comparisons against Python `hashlib` and `cryptography`: same keys, same packet bytes, each side opens the other's packets |
| Authentication | Every single-bit flip of a packet is rejected; wrong password, network name, iteration count or sender ID; truncated and extended packets; valid tag with invalid padding; 200,000 random packets |
| IVs | Never repeat across 300 packets even with a broken random source; identical plaintexts give different ciphertexts |
| End to end | Acknowledged unicast, broadcast and the full RollCall + PeerMessenger stack over an encrypted link; a node with the wrong password learns nothing and is not learned |
| Known limitation | A replayed packet is accepted (test documents this) |

### Seed sweeps (`test_sim_stress.cpp`, tag `[.stress]`)

A radio protocol is probabilistic, so single runs prove little. Each scenario
is run with 200 random seeds for both links and both radio models, and must
succeed in at least 97 to 99% of runs with no hangs and no duplicates.

| Scenario | Result (v2.0) |
|---|---|
| Acknowledged message by name between two nodes | 800 / 800 |
| Five nodes started together learn about each other within 3 minutes | 800 / 800 |
| Four nodes asking for the same name end up with unique names | 800 / 800 |
| Four nodes transmitting at the same instant, 8 tries each | 800 / 800 |
| Acknowledged message at SF10 and SF12 | 160 / 160 |

### Tooling

| Check | Command |
|---|---|
| Unit and scenario tests | `./build/tests/test_all` |
| Seed sweeps | `./build/tests/test_all "[.stress]"` |
| Memory errors and undefined behaviour | build with `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"` and run both of the above |
| Data races in the simulator | build with `-fsanitize=thread`, run with `setarch $(uname -m) -R` |
| Embedded build flags | library sources compile with `-std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wconversion -Wshadow` without warnings |
| Crypto cross-check | `python3 validate_encryption.py` |
| Target build | `examples/esp32_platformio` builds with PlatformIO for `heltec_wifi_lora_32_V3` |

## 3. Results

Version 1.0 against the new link scenarios: **26 of 37 failed** (every
acknowledged send on a windowed radio, duplicate delivery, early receive
timeouts, forged ACKs accepted, Basic and Backoff links unable to talk to each
other, broadcast with ACK always failing, payloads over-reported).

Version 2.0: all 190+ test cases pass, under AddressSanitizer,
UndefinedBehaviorSanitizer and ThreadSanitizer as well.

## 4. What the simulator cannot tell you: hardware checklist

The simulator models the channel, not the SX1262 or the vendor driver. These
need two or three boards. Each step says what to look for.

1. **Driver smoke test.** Flash `examples/esp32_platformio` to two boards.
   Both should print their name and ID; one of them renames itself (same
   firmware, same name). Expect each board to show the other within 5 s.
2. **Broadcast.** Press the button on board A. Board B shows the message once.
   Repeat 20 times at a slow pace: expect 20 of 20 at short range.
3. **Receive between calls.** This is the main unknown in the new driver: it
   relies on the SX1262 staying in continuous receive and on a frame that
   finished between two `receive()` calls being returned by the next one.
   If step 2 loses packets, print `radio.framesReceived()` and
   `radio.crcErrors()` on the receiver to see whether the radio heard them.
4. **Acknowledged unicast.** Change the button handler to
   `messenger.sendMessage("<other name>", "ping", true)` and print the result.
   Expect `true` nearly every time. `lora_link.stats().txRetransmits` shows
   how often a retry was needed.
5. **Half duplex and turnaround.** Hold both buttons so both boards send at
   once, repeatedly. Expect retries, no duplicates on the display, and no
   lock-up.
6. **Time on air.** Compare `radio.timeOnAirMs(9)` with the simulator's value
   for the same settings (45 ms at SF7 / 125 kHz / 12 symbol preamble).
7. **Range and slow settings.** Repeat step 4 at SF10 or SF12. The link takes
   its ACK timeout from `timeOnAirMs()`; if ACKs time out, that function is
   the first thing to check.
8. **Encryption.** Put `EncryptedLoRaLink` between the link and RollCall on
   both boards (as `EncNode` does in `tests/sim/SimNode.h`). A third board
   with a different password must see nothing. Note the start-up delay from
   key derivation.
9. **Long run.** Three boards, one message every 15 s each, for an hour.
   Count sent, acknowledged and received per board from the serial logs.

## 5. Not covered

- Capture effect (a strong frame surviving a collision), fading, frequency
  error and clock drift: the simulator treats every overlap as a loss and
  every non-overlapping frame as perfect unless random loss is switched on.
- Listen-before-talk. The library transmits without checking the channel.
- Regulatory duty-cycle limits. The application has to respect them.
- Power consumption and sleep modes.
- More than six nodes, and multi-hop topologies (the library does not relay).
