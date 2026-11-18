#!/usr/bin/env python3
"""Time every guard on the real board: resets it, reads the boot draw, then sends 'n' to step through the rest.

Usage: (cd firmware && cargo run --release) && python3 tools/drawtime.py [PORT]   (close any serial monitor first)
Relies on the firmware printing "guard N drawn in X ms" and stepping to the next guard on 'n'.
"""
import re, sys, time
import serial  # pyserial: see test/README.md for the venv

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"


def connect(deadline):
    # The ESP32-C3's USB serial drops and re-enumerates on reset, so keep retrying until it is back.
    while time.time() < deadline:
        try:
            return serial.Serial(PORT, 115200, timeout=0.2)
        except (serial.SerialException, OSError):
            time.sleep(0.05)
    sys.exit(f"{PORT} did not come back after reset")


def next_draw(p, deadline):
    buf = b""
    while time.time() < deadline:
        buf += p.read(256)
        m = re.search(rb"guard (\d+) drawn in (\d+) ms", buf)
        if m:
            return int(m[1]), int(m[2])
    sys.exit("no draw time on serial (is the firmware current?)")


p = serial.Serial(PORT, 115200)
p.dtr, p.rts = False, True  # USB-Serial-JTAG reset
time.sleep(0.1)
p.rts = False
p.close()

p = connect(time.time() + 10)
seen = {}
guard, ms = next_draw(p, time.time() + 10)
while guard not in seen:
    seen[guard] = ms
    print(f"guard {guard}: {ms} ms")
    p.write(b"n")
    guard, ms = next_draw(p, time.time() + 30)
