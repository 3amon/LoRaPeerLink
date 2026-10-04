# Hardware bench test

One firmware for two or more boards. Each board discovers the others, sends
an acknowledged `PING` to every peer every five seconds, checks what it
receives for gaps and duplicates, and reports everything on the serial
console. No display is needed.

Use it to verify the library and the `SemtechRadio` driver on real radios
(see `TEST_PLAN.md`, section 4, at the top of the repository).

## Boards

| Environment | Board | Console |
|---|---|---|
| `wireless_shell_v3` (default) | Heltec Wireless Shell V3 module on a carrier board (ESP32-S3 + SX1262) | ESP32-S3 native USB |
| `heltec_v3` | Heltec WiFi LoRa 32 V3, Wireless Stick V3 | CP2102 USB serial |
| `wireless_shell_v3_encrypted`, `heltec_v3_encrypted` | Same, with `EncryptedLoRaLink` in the stack | |

**Attach the LoRa antenna before powering a board.** Transmitting without one
can damage the radio.

Flashing replaces whatever firmware is on the board, including its partition
table.

## If the board stops after printing `ESP32ChipID=...`

The vendor radio stack (`libheltec.a`) refuses to start without a per-chip
Heltec license in flash. It prints the chip ID, asks for a license on UART0
and waits forever; on a board whose console is the native USB port you only
see the chip ID line and then nothing.

1. Copy the 12 hex digits after `ESP32ChipID=`.
2. Enter them at https://resource.heltec.cn/search to get the license: four
   32-bit numbers such as `0x11111111,0x22222222,0x33333333,0x44444444`.
3. Store the license where the library keeps it: 16 bytes (the four numbers,
   each little-endian) in the last 4 KB sector of the `app1` partition. With
   the default 8 MB partition table used here that is address `0x66F000`:

   ```bash
   python3 -c "import struct,sys; sys.stdout.buffer.write(struct.pack('<4I', 0x11111111,0x22222222,0x33333333,0x44444444))" > license.bin
   esptool.py --port /dev/ttyACM0 write_flash 0x66F000 license.bin
   ```

The license survives firmware uploads. It has to be written again after a
full-chip erase or if the partition table changes. A license is tied to one
chip; do not commit licenses to a public repository.

## Flash and watch

```bash
cd examples/esp32_bench_test
pio run -e wireless_shell_v3 -t upload --upload-port /dev/ttyACM0
pio run -e wireless_shell_v3 -t upload --upload-port /dev/ttyACM1

pio device monitor -p /dev/ttyACM0      # one terminal per board
pio device monitor -p /dev/ttyACM1
```

If the upload cannot connect, hold the button on GPIO0 while plugging the
board in (or while pressing reset) to enter the bootloader.

## What a healthy run looks like

```
========================================
 LoRaPeerLink bench test
 link: LoRaBackoffLink (not encrypted)
 node: bench-A1B2
 radio: 915.000 MHz, SF7, 125 kHz, 14 dBm
========================================
 time on air: 9 byte ACK 46 ms, 25 byte frame 67 ms, 255 byte frame 400 ms
 joined as bench-A1B2, id 51234
[   12.480] TX   PING 1 -> bench-C3D4 (20977)  ACK after 141 ms
[   13.902] RX   PING 1 <- bench-C3D4 (20977)  rssi -38 dBm
```

- **Time on air** for the 9 byte ACK should be 45 or 46 ms at the default
  settings. Anything else means the radio is not configured as expected.
- **Each board lists the other** within about five seconds (`n` command).
- **Pings are acknowledged** in roughly 120 to 250 ms with no retransmission
  most of the time.
- **`duplicates` stays at 0.** A duplicate means the receiver delivered a
  retransmitted frame twice.
- **`missing` on one board matches `failed` on the other.**
- **`radio frames received` grows.** If it stays at 0 while the other board
  is transmitting, the radio is not receiving at all: check the antenna and
  the board environment.
- **`crc errors`** should be rare at short range.
- **RSSI** between two boards on the same desk with antennas should be
  somewhere around -30 to -70 dBm. Values near -115 dBm mean the signal is
  barely above the noise floor: check the antennas.

## Commands

| Key | Action |
|---|---|
| `s` | Statistics now (also printed every 30 s) |
| `n` | List known peers |
| `p` | Ping all peers now |
| `f` | Flood: 10 pings in a row to every peer. Press it on two boards at the same moment to force collisions; expect retransmissions, no duplicates, no lock-up |
| `b` | Broadcast (the GPIO0 button does the same) |
| `m` | Send a maximum-length message (a full 255 byte frame on a plain link) to every peer; the receiver checks every byte |
| `q` | RF check: print the channel signal level for 6 s. Run it on one board while another does `x`; if the level does not rise, no radio energy is arriving (antenna, power, distance) |
| `x` | RF check: transmit a burst of 30 broadcast frames |
| `v` | Toggle the library's own log (RollCall and PeerMessenger traffic) |
| `r` | Reset counters |

## Settings

Override on the command line, for example to test a slow modem setting:

```bash
PLATFORMIO_BUILD_FLAGS="-D BENCH_SPREADING_FACTOR=10" pio run -e wireless_shell_v3 -t upload
```

`BENCH_FREQUENCY_HZ`, `BENCH_SPREADING_FACTOR`, `BENCH_TX_POWER_DBM`,
`BENCH_PING_INTERVAL_MS`, `BENCH_STATS_INTERVAL_MS`, `BENCH_NETWORK`,
`BENCH_PASSWORD`, and `BENCH_SAME_NAME` (every board asks for the name
"twin", to watch the name collision being resolved). Every board must use the same radio settings, and for the
encrypted variants the same network name and password.
