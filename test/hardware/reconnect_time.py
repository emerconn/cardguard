#!/usr/bin/env python3
"""How fast is the board found and reconnected after waking? (board on USB, no serial monitor open)

Each trial: connect over BLE, send 'z' on serial (board sleeps 5 s, wakes by timer), then time from
waking to the first advert this PC hears and to a working connection again. Waking is taken as the
"power: sleep" line + 5 s: USB serial drops the board's own "power: woke" line while it comes back from light sleep.

Run: .venv/bin/python test/hardware/reconnect_time.py [TRIALS] [PORT]   (setup: see smoke.py)
"""
import asyncio, statistics, sys, time

import serial
from bleak import BleakClient, BleakScanner

TRIALS = int(sys.argv[1]) if len(sys.argv) > 1 else 5
PORT = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM0"


SLEEP_S = 5  # matches Cmd::Sleep(5) for 'z' in firmware/src/main.rs


def sleep_board():
    """Sends 'z' on a fresh serial handle (light sleep breaks the old one); returns when it wakes."""
    end = time.monotonic() + 10
    while True:
        try:
            p = serial.Serial(PORT, 115200, timeout=0.2)
            break
        except (serial.SerialException, OSError):
            if time.monotonic() > end:
                sys.exit(f"{PORT} not available")
            time.sleep(0.1)
    p.reset_input_buffer()
    p.write(b"z")
    while time.monotonic() < end:
        if p.readline().decode(errors="replace").strip() == "power: sleep":
            p.close()
            return time.monotonic() + SLEEP_S
    sys.exit("board never logged 'power: sleep' (is the firmware current?)")


async def main():
    device = await BleakScanner.find_device_by_name("cardguard", timeout=15)
    if not device:
        sys.exit("cardguard not advertising")
    advert, connect = [], []
    for trial in range(1, TRIALS + 1):
        async with BleakClient(device):
            woke = sleep_board()  # connected while it falls asleep, like the phone
        seen_at = []
        heard = asyncio.Event()

        def on_advert(d, _):
            if d.address == device.address and time.monotonic() >= woke and not heard.is_set():
                seen_at.append(time.monotonic())
                heard.set()

        async with BleakScanner(on_advert):  # listening through the sleep, so the first advert counts
            await asyncio.wait_for(heard.wait(), SLEEP_S + 30)
        async with BleakClient(device, timeout=20):
            connected = time.monotonic()
        advert.append(seen_at[0] - woke)
        connect.append(connected - woke)
        print(f"trial {trial}: first advert {advert[-1]:.2f} s after waking, connected {connect[-1]:.2f} s")
        await asyncio.sleep(1)
    print(f"median: first advert {statistics.median(advert):.2f} s, connected {statistics.median(connect):.2f} s")


asyncio.run(main())
