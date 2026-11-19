#!/usr/bin/env python3
"""Does Bluetooth wait for a guard draw? (board on USB, no serial monitor open)

Times back-to-back BLE reads of the brightness value for a few seconds while the board idles, then
while serial 'n' keeps it drawing guards. If BLE waits for the CPU-bound draw, the slowest reads during
draws grow by roughly the length of one render.

Run: .venv/bin/python test/hardware/ble_latency.py [SECONDS] [PORT]   (setup: see smoke.py)
"""
import asyncio, statistics, sys, threading, time

import serial
from bleak import BleakClient, BleakScanner

SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 8
PORT = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM0"
BRIGHT = "38c7816a-72f6-40eb-b1ab-b01bb0136afc"


async def reads(c, seconds):
    times, end = [], time.monotonic() + seconds
    while time.monotonic() < end:
        t = time.monotonic()
        await c.read_gatt_char(BRIGHT)
        times.append((time.monotonic() - t) * 1000)
    return times


def report(name, t):
    t = sorted(t)
    print(f"{name:7} n={len(t):4}  median {statistics.median(t):5.0f} ms  p95 {t[int(len(t) * 0.95)]:5.0f} ms  max {t[-1]:5.0f} ms")


def keep_drawing(port, stop):
    """Sends 'n' after each "drawn in" line, so the board draws back to back."""
    with serial.Serial(port, 115200, timeout=0.5) as p:
        p.reset_input_buffer()
        p.write(b"n")
        while not stop.is_set():
            if "drawn in" in p.readline().decode(errors="replace"):
                p.write(b"n")


async def main():
    device = await BleakScanner.find_device_by_name("cardguard", timeout=15)
    if not device:
        sys.exit("cardguard not advertising")
    async with BleakClient(device) as c:
        await reads(c, 1)  # settle the connection
        report("idle", await reads(c, SECONDS))
        stop = threading.Event()
        drawer = threading.Thread(target=keep_drawing, args=(PORT, stop))
        drawer.start()
        try:
            report("drawing", await reads(c, SECONDS))
        finally:
            stop.set()
            drawer.join()


asyncio.run(main())
