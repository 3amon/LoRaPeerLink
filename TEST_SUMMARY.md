# Test Summary

This file used to describe the version 1.0 unit tests. The current test
strategy, coverage and results are in **[TEST_PLAN.md](TEST_PLAN.md)**.

Quick reference:

```bash
cmake -S . -B build && cmake --build build -j
./build/tests/test_all                 # unit tests and simulated-radio scenarios
./build/tests/test_all "[.stress]"     # seed sweeps (200 seeds per scenario)
python3 validate_encryption.py         # crypto cross-check
```

| File | What it covers |
|---|---|
| `tests/sim/` | The radio simulator (virtual time, time on air, half duplex, collisions, loss) |
| `test_sim_radio.cpp` | Tests of the simulator itself |
| `test_sim_link.cpp` | Link layer scenarios on the simulated channel |
| `test_sim_rollcall.cpp` | Discovery, collisions and hostile input |
| `test_sim_messenger.cpp` | End-to-end messaging, including a lossy soak test |
| `test_sim_stress.cpp` | Seed sweeps (hidden by default) |
| `test_crypto.cpp` | AES, SHA-256, HMAC, PBKDF2 against standard vectors |
| `test_encrypted_link.cpp` | Encrypted packet format, tampering, end to end |
| `test_simple_link.cpp`, `test_backoff_link.cpp`, `test_packet_framing.cpp`, `test_backoff_packet_framing.cpp`, `test_crc.cpp`, `test_interface.cpp` | Frame format and link API on the simple mock |
| `test_rollcall.cpp`, `test_peer_messenger.cpp` | RollCall and PeerMessenger rules on the simple mock |
| `test_timeout_propagation.cpp` | Timeouts are passed down through the layers |
| `test_mock_radio.cpp` | The simple mock itself |
