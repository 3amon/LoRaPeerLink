# Changelog

## 2.0.0

Version 1.0 passed its unit tests but did not work reliably on real radios.
The tests ran against a mock in which packets arrived instantly, were never
lost, and time did not exist. Version 2.0 adds a simulator of a half-duplex
LoRa channel (see [TEST_PLAN.md](TEST_PLAN.md)), fixes what it exposed, and
replaces the placeholder encryption with a real implementation.

**Version 2.0 nodes cannot talk to version 1.0 nodes.** The Backoff link's
frame format and the encryption both changed.

### Bugs fixed: link layer

- **Acknowledgments could not be received on a real radio.** The sender
  polled for the ACK in 20 ms receive windows with 5 ms sleeps in between. An
  ACK is on the air for about 45 ms at SF7 (over a second at SF12), and a
  driver that restarts its receiver for every window can never capture a
  frame longer than the window. Every acknowledged send failed. The link now
  waits in one long window.
- **`LoRaBasicLink` ignored `maxRetries`.** It transmitted once. Both links
  now retry.
- **Retries collided again and again.** After a collision both senders
  waited the same time and retried together. Retries now use binary
  exponential backoff scaled to the frame's time on air.
- **A lost ACK delivered the payload twice** (Backoff link). Retransmissions
  now reuse the sequence number and the receiver suppresses duplicates.
- **Frames received while waiting for an ACK were thrown away.** Two nodes
  sending to each other both failed. Such frames are now acknowledged and
  queued for the next `receivePacket()`.
- **Any frame with the ACK flag and a matching sequence number counted as an
  ACK** (Basic link): no CRC check, no address check. The Backoff link
  compared against the local ID, so it failed whenever `setLocalId()` had not
  been called. ACKs are now fully validated against the packet that was sent.
- **Basic and Backoff links could not talk to each other.** The Backoff link
  wrote its header in host byte order. Both now use the documented big-endian
  format (shared implementation in `LoRaLinkCore`).
- **The maximum payload produced a 256 byte frame**, one more than a LoRa
  radio can carry (the driver's length byte wrapped to 0). `MAX_PAYLOAD` is
  now 246.
- **`receivePacket()` returned early** when it heard a frame for another node
  or a corrupt frame, instead of listening for its whole timeout.
- **`receivePacket()` reported more bytes than it wrote** (Basic link): a
  payload larger than the caller's buffer was truncated but the full length
  was returned. Such a payload is now dropped and not acknowledged.
- **Broadcast with `requestAck`** waited for an ACK that nobody sends and
  returned failure. Broadcasts are never acknowledged now.
- **`LoRaBackoffLink` with `maxRetries` 0 sent nothing** and reported
  success. At least one transmission is always made.
- **The "random" backoff was the clock modulo a constant**, identical on two
  nodes started together.

### Bugs fixed: RollCall

- **A malformed packet could crash the node.** IDs were parsed with
  `std::stoul`, which throws on text such as `HELLOIAM|x AT abc`. All input
  from the radio is now validated without exceptions.
- **User messages were dropped** whenever RollCall, not PeerMessenger, was
  the one listening: during `begin()`, during `whoIs()`, and in applications
  that call both `processMessages()` functions. RollCall now passes other
  payloads to a data handler, which PeerMessenger registers.
- **Periodic announcements stopped** when the application only called
  `PeerMessenger::processMessages()`.
- **`begin()`, `whoIs()` and `whereIs()` slept between short polls**, so the
  radio was deaf most of the time it was supposed to be listening.
- **Name collisions renamed both nodes**, and the announcement period was
  fixed, so two nodes that collided once collided every time. Collision
  handling is now deterministic (exactly one node changes), announcements go
  out at randomised times, and newcomers are answered within seconds.
- **A node with the same name and the same ID was undetectable** (the check
  compared the link source ID with the node's own ID, which are equal in that
  case). Announcements now carry a random nonce.
- **Every node that knew an answer replied to `WHOIS`**, all at once. Only
  the owner of the name or ID answers now.
- Names containing `" AT "` were mis-parsed; the peer table grew without
  limit; stale entries survived a node changing its ID; a random source that
  returned a constant hung the node.

### Bugs fixed: PeerMessenger

- **Messages longer than the payload were silently truncated**, and messages
  over 255 bytes wrapped around to a short length. Over-long messages are now
  refused.
- The receive queue was unbounded.

### Bugs fixed: encryption

`EncryptedLoRaLink` 1.0 documented AES-128-CBC with PBKDF2 but contained
placeholders:

- The "key derivation" was a 32-bit non-cryptographic hash; 12 of the 16 key
  bytes were always zero.
- The "cipher" XORed the payload with that key and with the IV, which is sent
  in the clear. XORing a captured packet with its own IV revealed most of the
  message.
- There was no integrity check: any packet, from any network, was accepted.
- A long password and network name overflowed a stack buffer.
- `keyIterations` was ignored.

Version 2.0 implements AES-128-CBC with HMAC-SHA256 authentication
(encrypt-then-MAC) and PBKDF2-HMAC-SHA256, in dependency-free C++ verified
against the standard test vectors and against Python's `cryptography`
package. See `include/EncryptedLoRaLink.h` for the format and for what it
does not protect against (replay, traffic analysis).

### ESP32 example

- `SemtechRadio.h/.cpp` were missing, so the example did not build. They are
  back, rewritten to keep the radio in continuous receive instead of
  restarting the receiver on every `receive()` call (which aborted frames in
  flight) and leaving it idle after every transmission. **Compiled, not yet
  run on hardware.**
- `main.cpp` now uses `PeerMessenger` for everything. The old loop read
  packets with the link directly, then called RollCall, then slept for a
  second with the radio idle.

### API changes

- New: `LoRaLinkCore` (base of both links) with `setAckTimeoutMs()`,
  `setDuplicateWindowMs()`, `stats()`, `pendingCount()`.
- New: `ILoRaLink::maxPayloadSize()`, `IRadio::timeOnAirMs()` (optional).
- New: `RollCall::setDataHandler()`, `isValidName()`, `parseNodeId()`.
- New: `PeerMessenger::maxMessageLength()`, `droppedMessages()`.
- `EncryptedLoRaLink` constructor takes an optional clock and random source;
  `seal()` / `open()` expose the packet format.
- `MAX_PAYLOAD` is 246 (was 247). Encrypted payload limit is 207 (was 168).
- `receivePacket()` returns 0 for a payload that does not fit the buffer.
- `maxRetries` is the total number of transmissions, minimum 1, for both
  links.
- Node names: 1 to 48 bytes, no `|`, no control characters.
- RollCall no longer answers `WHOIS` / `WHEREIS` on behalf of other nodes.
- Source compatible for typical use: the WirelessStick application builds
  against 2.0 unchanged.

### Tests

- `tests/sim/`: simulated LoRa channel.
- `MockRadio` no longer lets a node receive its own transmissions.
- 66 test cases became 190+; see [TEST_PLAN.md](TEST_PLAN.md).
