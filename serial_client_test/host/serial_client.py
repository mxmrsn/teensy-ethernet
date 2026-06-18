#!/usr/bin/env python3
"""Standalone host-side serial client for the Teensy 4.1 comms baseline.

Talks the plain newline ASCII protocol implemented in ../src/main.cpp -- NO ROS.
Use it to confirm the link works, measure round-trip latency and throughput, and
poke the galvo/trigger/telemetry paths before bringing micro-ROS into the mix.

Examples (Windows -- adjust the COM port):
    python serial_client.py --port COM5 id
    python serial_client.py --port COM5 ping --count 1000
    python serial_client.py --port COM5 galvo 1024 3072
    python serial_client.py --port COM5 trig 50
    python serial_client.py --port COM5 monitor --hz 200
    python serial_client.py --port COM5 bench --lines 200000
    python serial_client.py --port COM5 repl

Requires: pyserial  (pip install pyserial)
"""

import argparse
import statistics
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial not installed -- run:  pip install pyserial")


class SerialClient:
    """Thin wrapper over a pyserial port speaking the line protocol."""

    def __init__(self, port, baud=115200, timeout=1.0):
        # On Teensy USB CDC the baud is ignored, but pyserial still needs one.
        self.ser = serial.Serial(port, baud, timeout=timeout)
        # Teensy reboots/asserts DTR on open; give it a moment to come up.
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def send(self, cmd):
        self.ser.write((cmd.strip() + "\n").encode("ascii"))
        self.ser.flush()

    def readline(self):
        return self.ser.readline().decode("ascii", errors="replace").rstrip("\r\n")

    def command(self, cmd):
        """Send a command and return the first non-telemetry reply line."""
        self.send(cmd)
        while True:
            line = self.readline()
            if line == "":
                return None  # timeout
            if line.startswith("TLM "):
                continue     # skip async telemetry while waiting for a reply
            return line


def cmd_id(client, _args):
    print(client.command("ID?") or "(no reply -- check port/firmware)")


def cmd_ping(client, args):
    """Round-trip latency benchmark."""
    rtts = []
    misses = 0
    print(f"pinging {args.count} times...")
    for seq in range(args.count):
        t0 = time.perf_counter()
        reply = client.command(f"PING {seq}")
        t1 = time.perf_counter()
        if reply and reply.startswith("PONG"):
            parts = reply.split()
            if len(parts) >= 2 and parts[1] == str(seq):
                rtts.append((t1 - t0) * 1e3)  # ms
                continue
        misses += 1

    if not rtts:
        print("no responses -- is the firmware running on this port?")
        return
    rtts.sort()
    p = lambda q: rtts[min(len(rtts) - 1, int(q * len(rtts)))]
    print(f"  samples : {len(rtts)}  (misses: {misses})")
    print(f"  min/avg : {rtts[0]:.3f} / {statistics.mean(rtts):.3f} ms")
    print(f"  p50/p99 : {p(0.50):.3f} / {p(0.99):.3f} ms")
    print(f"  max     : {rtts[-1]:.3f} ms")
    if len(rtts) > 1:
        print(f"  jitter  : {statistics.pstdev(rtts):.3f} ms (stdev)")


def cmd_galvo(client, args):
    print(client.command(f"GALVO {args.x} {args.y}"))


def cmd_trig(client, args):
    print(client.command(f"TRIG {args.ms}"))


def cmd_monitor(client, args):
    """Start telemetry streaming and print lines until Ctrl-C."""
    print(client.command(f"STREAM {args.hz}"))
    print("streaming telemetry -- Ctrl-C to stop")
    count, t_start = 0, time.perf_counter()
    try:
        while True:
            line = client.readline()
            if line.startswith("TLM "):
                count += 1
                print(line)
    except KeyboardInterrupt:
        pass
    finally:
        client.send("STREAM 0")
        dt = time.perf_counter() - t_start
        if dt > 0:
            print(f"\n{count} lines in {dt:.2f}s  ({count / dt:.0f} lines/s)")


def cmd_bench(client, args):
    """Throughput test: ask for N lines as fast as the link allows.

    Drains the port in bulk chunks rather than line-by-line. This matters:
    Teensy USB CDC drops queued TX data after an internal timeout if the host
    falls behind, so a slow byte-by-byte reader can lose the tail of the stream
    (including the 'BENCH DONE' marker) and hang. Bulk reads keep us ahead.
    """
    client.ser.reset_input_buffer()
    t0 = time.perf_counter()
    client.send(f"BENCH {args.lines}")
    buf = bytearray()
    marker = b"BENCH DONE"
    while marker not in buf:
        # Block for at least 1 byte (honors the port timeout), then sweep up
        # everything else already waiting in the OS buffer in one go.
        chunk = client.ser.read(1)
        if not chunk:
            print(f"timeout -- got {buf.count(b'TLM ')} lines "
                  f"({len(buf)} bytes) before stall")
            return
        waiting = client.ser.in_waiting
        if waiting:
            chunk += client.ser.read(waiting)
        buf += chunk
    dt = time.perf_counter() - t0
    got = buf.count(b"TLM ")
    print(f"  received : {got} lines, {len(buf)} bytes in {dt:.3f}s")
    if dt > 0:
        print(f"  rate     : {got / dt:.0f} lines/s, "
              f"{len(buf) / dt / 1024:.1f} KiB/s")


def cmd_repl(client, _args):
    """Interactive prompt -- type raw protocol commands."""
    print("interactive mode -- type commands (e.g. 'PING 1'), 'quit' to exit")
    try:
        while True:
            line = input("> ").strip()
            if line.lower() in ("quit", "exit"):
                break
            if not line:
                continue
            reply = client.command(line)
            print(reply if reply is not None else "(timeout)")
    except (EOFError, KeyboardInterrupt):
        print()


def build_parser():
    p = argparse.ArgumentParser(description="Teensy standalone serial client (no ROS)")
    p.add_argument("--port", required=True, help="serial port, e.g. COM5 or /dev/ttyACM0")
    p.add_argument("--baud", type=int, default=115200, help="baud (ignored by USB CDC)")
    p.add_argument("--timeout", type=float, default=1.0, help="read timeout seconds")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("id").set_defaults(func=cmd_id)

    sp = sub.add_parser("ping", help="round-trip latency benchmark")
    sp.add_argument("--count", type=int, default=1000)
    sp.set_defaults(func=cmd_ping)

    sp = sub.add_parser("galvo", help="set galvo X Y (0-4095)")
    sp.add_argument("x", type=int)
    sp.add_argument("y", type=int)
    sp.set_defaults(func=cmd_galvo)

    sp = sub.add_parser("trig", help="fire trigger for MS milliseconds")
    sp.add_argument("ms", type=int)
    sp.set_defaults(func=cmd_trig)

    sp = sub.add_parser("monitor", help="stream telemetry at HZ until Ctrl-C")
    sp.add_argument("--hz", type=int, default=100)
    sp.set_defaults(func=cmd_monitor)

    sp = sub.add_parser("bench", help="throughput test of N lines")
    sp.add_argument("--lines", type=int, default=100000)
    sp.set_defaults(func=cmd_bench)

    sub.add_parser("repl", help="interactive command prompt").set_defaults(func=cmd_repl)
    return p


def main():
    args = build_parser().parse_args()
    client = SerialClient(args.port, args.baud, args.timeout)
    try:
        args.func(client, args)
    finally:
        client.close()


if __name__ == "__main__":
    main()
