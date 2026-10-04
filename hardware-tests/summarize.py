#!/usr/bin/env python3
"""
Summarise bench-test logs written by run_bench.py.

Usage: python3 hardware-tests/summarize.py <log dir>      (prints Markdown)
"""
import glob
import os
import re
import sys

LINE = re.compile(r"^\s*([0-9.]+) ([AB])\| (.*)$")
TX_PING = re.compile(r"TX   PING (\d+) -> (\S+) \((\d+)\)\s+(ACK after (\d+) ms|NO ACK after (\d+) ms)(.*)")
RX_PING = re.compile(r"RX   PING (\d+) <- (\S+) \((\d+)\)\s+rssi (-?\d+) dBm(.*)")
TX_BIG = re.compile(r"TX   BIG (\d+) \((\d+) bytes\) -> .*?(ACK|NO ACK) after (\d+) ms")
RX_BIG = re.compile(r"RX   BIG (\d+) \((\d+) bytes\) <- .*?(content intact|CORRUPT)")
STAT = re.compile(r"^\s{2}(\S.*?)\s{2,}(.*)$")


def median(values):
    values = sorted(values)
    return values[len(values) // 2] if values else 0


def percentile(values, p):
    values = sorted(values)
    return values[min(len(values) - 1, int(len(values) * p))] if values else 0


def load(path):
    rows = []
    header = []
    for raw in open(path, encoding="utf-8", errors="replace"):
        if raw.startswith("#"):
            header.append(raw.strip())
            continue
        m = LINE.match(raw.rstrip("\n"))
        if m:
            rows.append((float(m.group(1)), m.group(2), m.group(3)))
    return header, rows


def ping_stats(rows, lo=0.0, hi=1e12):
    out = {}
    for board in "AB":
        sent = acked = failed = retried = 0
        times = []
        received = dup = gaps = 0
        rssi = []
        for t, b, text in rows:
            if b != board or not (lo <= t < hi):
                continue
            m = TX_PING.search(text)
            if m:
                sent += 1
                if m.group(5):
                    acked += 1
                    times.append(int(m.group(5)))
                    if "retransmission" in m.group(7):
                        retried += 1
                else:
                    failed += 1
                continue
            m = RX_PING.search(text)
            if m:
                received += 1
                rssi.append(int(m.group(4)))
                if "DUPLICATE" in m.group(5):
                    dup += 1
        out[board] = dict(sent=sent, acked=acked, failed=failed, retried=retried, times=times,
                          received=received, dup=dup, rssi=rssi)
    return out


def last_stats_block(rows, board):
    block = {}
    current = None
    for t, b, text in rows:
        if b != board:
            continue
        if text.startswith("===="):
            current = {"_title": text.strip("= ").strip()}
            block = current
        elif current is not None and text.startswith("  "):
            current.setdefault("_lines", []).append(text.strip())
    return block


def fmt_ping_row(label, s):
    total = s["acked"] + s["failed"]
    pct = 100.0 * s["acked"] / total if total else 0.0
    return (f"| {label} | {total} | {s['acked']} ({pct:.1f}%) | {s['retried']} | {s['failed']} | "
            f"{median(s['times'])} / {percentile(s['times'], 0.95)} / {max(s['times']) if s['times'] else 0} | "
            f"{s['received']} | {s['dup']} |")


def summarize_phase(path):
    name = os.path.basename(path)[:-4]
    header, rows = load(path)
    lines = [f"### {name}", ""]
    for h in header:
        if h.startswith("# phase") and "finished" not in h:
            lines.append("`" + h[2:] + "`")
            lines.append("")
    if not rows:
        return lines + ["No data.", ""]

    duration = rows[-1][0]
    resets = [t for t, b, text in rows if "resetting this board" in text]
    floods = []   # flood = many TX PING lines in a short time; found from the schedule instead
    lines.append("| Board | Pings finished | Acknowledged | Needed a retransmission | Failed | Time to ACK ms (median / 95% / max) | Pings received | Delivered twice |")
    lines.append("|---|---|---|---|---|---|---|---|")
    whole = ping_stats(rows)
    for board in "AB":
        lines.append(fmt_ping_row(board, whole[board]))
    lines.append("")

    # Maximum-size messages and broadcasts.
    for board in "AB":
        other = "B" if board == "A" else "A"
        big_tx = [(m.group(3), int(m.group(4)), int(m.group(2))) for t, b, text in rows if b == board for m in [TX_BIG.search(text)] if m]
        big_rx = [(m.group(3), int(m.group(2))) for t, b, text in rows if b == other for m in [RX_BIG.search(text)] if m]
        if big_tx:
            acked = sum(1 for r in big_tx if r[0] == "ACK")
            intact = sum(1 for r in big_rx if r[0] == "content intact")
            corrupt = sum(1 for r in big_rx if r[0] == "CORRUPT")
            lines.append(f"- Maximum-size messages from {board} ({big_tx[0][2]} bytes of text): {len(big_tx)} sent, {acked} acknowledged, "
                         f"{intact} received intact, {corrupt} corrupt. Time to ACK: {', '.join(str(r[1]) for r in big_tx)} ms.")
        bc_tx = sum(1 for t, b, text in rows if b == board and "TX   BCAST" in text and text.rstrip().endswith("sent"))
        bc_rx = sum(1 for t, b, text in rows if b == other and "RX   BCAST" in text)
        if bc_tx:
            lines.append(f"- Broadcasts from {board}: {bc_tx} sent, {bc_rx} received by the other board (broadcasts are not acknowledged or retried).")
    lines.append("")

    # Radio configuration lines and identities.
    for t, b, text in rows:
        if "time on air:" in text and b == "A":
            lines.append(f"- Radio reports{text.split('time on air:')[1].rstrip()} on air.")
            break
    for t, b, text in rows:
        if "key derivation took" in text and b == "A":
            lines.append(f"- {text.strip().capitalize()}.")
            break
    joined = [(t, b, text.strip()) for t, b, text in rows if "joined as" in text]
    names = {}
    for t, b, text in joined:
        names.setdefault(b, []).append(text.replace("joined as ", ""))
    for b in "AB":
        if b in names:
            lines.append(f"- Board {b} joined as: {'; then, after its restart, as '.join(names[b])}.")

    # Restart recovery.
    for reset_at in resets:
        after = ping_stats(rows, reset_at, 1e12)
        # A's first acknowledged ping to B's new ID (the ID in the log line tells it apart from a
        # ping that was acknowledged just before the reset).
        new_id = re.search(r"id (\d+)", names.get("B", [""])[-1])
        new_id = f"({new_id.group(1)})" if new_id else "("
        first_ok = next((t for t, b, text in rows if t > reset_at and b == "A" and TX_PING.search(text)
                         and "ACK after" in text and new_id in text), None)
        first_b = next((t for t, b, text in rows if t > reset_at + 3 and b == "B" and "ACK after" in text), None)
        lines.append(f"- Board B was reset at {reset_at:.0f} s. First acknowledged ping afterwards: from B after "
                     f"{(first_b - reset_at) if first_b else float('nan'):.0f} s, from A after {(first_ok - reset_at) if first_ok else float('nan'):.0f} s "
                     f"(log times are when the host received the line, accurate to a few seconds).")
        lines.append(f"- After the restart: A {after['A']['acked']}/{after['A']['acked'] + after['A']['failed']} acknowledged, "
                     f"B {after['B']['acked']}/{after['B']['acked'] + after['B']['failed']} acknowledged.")

    # Final statistics block from each board.
    lines.append("")
    for board in "AB":
        block = last_stats_block(rows, board)
        if block:
            lines.append(f"Last statistics block, board {board} ({block.get('_title', '')}):")
            lines.append("")
            lines.append("```")
            lines.extend(block.get("_lines", []))
            lines.append("```")
            lines.append("")
    rssi = whole["A"]["rssi"] + whole["B"]["rssi"]
    if rssi:
        lines.append(f"Received signal strength over the phase: {min(rssi)} to {max(rssi)} dBm (median {median(rssi)}).")
        lines.append("")
    return lines


def main():
    log_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    out = []
    for path in sorted(glob.glob(os.path.join(log_dir, "*.log"))):
        out.extend(summarize_phase(path))
    print("\n".join(out))


if __name__ == "__main__":
    main()
