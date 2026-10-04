#!/usr/bin/env python3
"""
Run a sequence of bench-test phases on two boards and log everything.

For each phase this script builds examples/esp32_bench_test with the phase's
settings, flashes both boards, then records both serial consoles for the
phase's duration while sending scheduled commands (see the bench README for
what the single-letter commands do). A board can also be reset part way
through a phase to test recovery from a restart.

Usage:
    uv run --with pyserial hardware-tests/run_bench.py <log dir> <port A> <port B> [phase ...]

Logs: <log dir>/<phase>.log, one line per console line:
    <seconds since phase start> <A|B>| <text>
"""
import os
import subprocess
import sys
import threading
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
BENCH = os.path.join(HERE, "..", "examples", "esp32_bench_test")


def every(start, step, end, key):
    return [(t, key) for t in range(start, end, step)]


# name, PlatformIO environment, extra build flags, duration [s], commands for A, commands for B, resets [(time, board)]
PHASES = [
    ("1-soak-sf7", "wireless_shell_v3", "", 3600,
     every(60, 300, 3590, "b") + [(600, "m"), (1500, "m"), (2400, "m"), (3300, "m"), (1200, "f"), (3590, "s")],
     every(180, 300, 3590, "b") + [(900, "m"), (1800, "m"), (2700, "m"), (1200, "f"), (3593, "s")],
     [(2100, "B")]),
    ("2-same-name", "wireless_shell_v3", "-D BENCH_SAME_NAME=1", 240,
     [(120, "n"), (232, "s")], [(121, "n"), (235, "s")], []),
    ("3-sf10", "wireless_shell_v3", "-D BENCH_SPREADING_FACTOR=10 -D BENCH_PING_INTERVAL_MS=8000", 900,
     [(120, "b"), (300, "m"), (890, "s")], [(200, "b"), (600, "m"), (893, "s")], []),
    ("4-sf12", "wireless_shell_v3", "-D BENCH_SPREADING_FACTOR=12 -D BENCH_PING_INTERVAL_MS=20000", 600,
     [(150, "b"), (585, "s")], [(300, "b"), (590, "s")], []),
    ("5-encrypted-sf7", "wireless_shell_v3_encrypted", "", 1200,
     [(120, "b"), (300, "m"), (900, "m"), (600, "f"), (1190, "s")],
     [(200, "b"), (450, "m"), (600, "f"), (1193, "s")], []),
]


def flash(env, flags, port, log):
    environ = dict(os.environ)
    if flags:
        environ["PLATFORMIO_BUILD_FLAGS"] = flags
    else:
        environ.pop("PLATFORMIO_BUILD_FLAGS", None)
    result = subprocess.run(["pio", "run", "-d", BENCH, "-e", env, "-t", "upload", "--upload-port", port],
                            env=environ, capture_output=True, text=True)
    ok = result.returncode == 0
    log.write(f"# flash {port} env={env} flags='{flags}': {'ok' if ok else 'FAILED'}\n")
    if not ok:
        log.write(result.stdout[-2000:] + result.stderr[-2000:] + "\n")
    log.flush()
    return ok


def run_phase(name, env, flags, duration, cmds_a, cmds_b, resets, ports, log_dir):
    path = os.path.join(log_dir, name + ".log")
    with open(path, "w") as log:
        log.write(f"# phase {name}: env={env} flags='{flags}' duration={duration}s started {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        for port in ports:
            if not flash(env, flags, port, log):
                return False
        time.sleep(2)

        lock = threading.Lock()
        t0 = time.time()
        stop = threading.Event()

        def reader(tag, port, commands, my_resets):
            s = serial.Serial()
            s.port = port
            s.baudrate = 115200
            s.timeout = 0.1
            s.dtr = False
            s.rts = False
            s.open()
            pending = sorted(commands)
            pending_resets = sorted(my_resets)
            buf = b""
            while not stop.is_set():
                try:
                    buf += s.read(4096)
                except serial.SerialException as e:
                    with lock:
                        log.write(f"{time.time() - t0:8.1f} {tag}| # serial error: {e}\n")
                    time.sleep(1)
                    try:
                        s.close()
                        s.open()
                    except Exception:
                        pass
                    continue
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", "replace").rstrip()
                    if text and not text.startswith("ESP32ChipID="):
                        with lock:
                            log.write(f"{time.time() - t0:8.1f} {tag}| {text}\n")
                            log.flush()
                now = time.time() - t0
                while pending and pending[0][0] <= now:
                    s.write(pending.pop(0)[1].encode())
                while pending_resets and pending_resets[0] <= now:
                    pending_resets.pop(0)
                    with lock:
                        log.write(f"{now:8.1f} {tag}| # resetting this board (restart test)\n")
                    s.rts = True
                    time.sleep(0.2)
                    s.rts = False
            s.close()

        threads = [
            threading.Thread(target=reader, args=("A", ports[0], cmds_a, [t for t, b in resets if b == "A"])),
            threading.Thread(target=reader, args=("B", ports[1], cmds_b, [t for t, b in resets if b == "B"])),
        ]
        for t in threads:
            t.start()
        time.sleep(duration)
        stop.set()
        for t in threads:
            t.join()
        log.write(f"# phase {name} finished {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    return True


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    log_dir, ports, wanted = sys.argv[1], sys.argv[2:4], sys.argv[4:]
    os.makedirs(log_dir, exist_ok=True)
    progress = os.path.join(log_dir, "progress.txt")
    for phase in PHASES:
        if wanted and phase[0] not in wanted:
            continue
        with open(progress, "a") as p:
            p.write(f"{time.strftime('%H:%M:%S')} START {phase[0]}\n")
        ok = run_phase(*phase, ports, log_dir)
        with open(progress, "a") as p:
            p.write(f"{time.strftime('%H:%M:%S')} {'DONE' if ok else 'FAILED'} {phase[0]}\n")
    with open(progress, "a") as p:
        p.write(f"{time.strftime('%H:%M:%S')} ALL_DONE\n")


if __name__ == "__main__":
    main()
