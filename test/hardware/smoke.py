#!/usr/bin/env python3
"""Hardware smoke test: run after flashing, with the board on USB and no serial monitor open.

Resets the board, then checks over USB serial and Bluetooth that it boots, advertises, sends its
guard names and firmware version, draws a valid pick, rejects an invalid one, takes a brightness write, and streams log
lines over BLE. Leaves the board on guard 0 at its saved brightness.

Setup once:  python3 -m venv .venv && .venv/bin/pip install bleak pyserial
Run:         (cd firmware && cargo run --release) && .venv/bin/python test/hardware/smoke.py [PORT]
"""
import asyncio, re, sys, threading, time

import serial
from bleak import BleakClient, BleakScanner

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
SELECT = "6eeb6fbb-9458-47d6-9379-e73cab7dfa04"
NAMES = "ab3ba50e-45cb-4145-ae7e-3066f7e872a8"
BRIGHT = "38c7816a-72f6-40eb-b1ab-b01bb0136afc"
LOG = "7c0bd792-ba8a-4791-be95-3c354c4c2fd2"
VERSION = "30598302-24ed-4ff4-8c09-c9091182cab0"

serial_lines, ble_lines = [], []
failures = []


def check(what, ok, detail=""):
    print(f"{'ok  ' if ok else 'FAIL'} {what}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(what)


def reset_and_read_serial():
    """Resets the board (USB-Serial-JTAG, as tools/drawtime.py) and keeps reading lines into serial_lines."""
    p = serial.Serial(PORT, 115200)
    p.dtr, p.rts = False, True
    time.sleep(0.1)
    p.rts = False
    p.close()
    deadline = time.time() + 10
    while True:  # the C3's USB serial re-enumerates on reset
        try:
            p = serial.Serial(PORT, 115200, timeout=0.2)
            break
        except (serial.SerialException, OSError):
            if time.time() > deadline:
                sys.exit(f"{PORT} did not come back after reset")
            time.sleep(0.05)

    def pump():
        while True:
            try:
                line = p.readline()
            except (serial.SerialException, OSError):
                return
            if line:
                serial_lines.append(line.decode(errors="replace").strip())

    threading.Thread(target=pump, daemon=True).start()


async def wait_for(lines, pattern, timeout):
    """Waits until a line in `lines` matches; returns the match or None."""
    end = time.time() + timeout
    while time.time() < end:
        for line in lines:
            if m := re.search(pattern, line):
                return m
        await asyncio.sleep(0.05)
    return None


async def main():
    reset_and_read_serial()
    check("boots and draws guard 0", await wait_for(serial_lines, r"guard 0 drawn in \d+ ms", 10))
    check("prints its version", await wait_for(serial_lines, r"^cardguard \S+$", 5))
    heap = await wait_for(serial_lines, r"heap: (\d+) KB free", 5)
    check("prints free heap", heap)

    device = await BleakScanner.find_device_by_name("cardguard", timeout=15)
    check("advertises as cardguard", device)
    if not device:
        return
    async with BleakClient(device) as c:
        await c.start_notify(LOG, lambda _, data: ble_lines.append(data.decode(errors="replace")))
        guard_updates = []  # as text, so wait_for can match them
        await c.start_notify(SELECT, lambda _, data: guard_updates.append(str(data[0])))
        check("log subscribe is logged", await wait_for(ble_lines, r"subscribed to the log", 3))
        replayed = await wait_for(ble_lines, r"^([0-9a-f]{4}\.\d+) cardguard \S+$", 3)  # "<boot>.<n> line"
        check("boot lines replay on subscribe, numbered", replayed, ble_lines)

        names = (await c.read_gatt_char(NAMES)).decode().split("\n")
        check("sends guard names", len(names) >= 1 and all(names), repr(names))
        version = (await c.read_gatt_char(VERSION)).decode()
        check("sends its firmware version", re.fullmatch(r"v\d+\.\d+\.\d+(-\d+-g[0-9a-f]+)?|[0-9a-f]{7,}", version), repr(version))

        last = len(names) - 1
        serial_lines.clear(), ble_lines.clear()
        await c.write_gatt_char(SELECT, bytes([last]), response=True)
        check(f"valid pick draws guard {last}", await wait_for(serial_lines, rf"guard {last} drawn in \d+ ms", 3))
        check("pick arrives in the BLE log", await wait_for(ble_lines, rf"picked guard {last}", 3))
        # notified right after the "drawn" serial line, so it can trail it a little
        check("guard change is notified", await wait_for(guard_updates, rf"^{last}$", 2), guard_updates)
        current = (await c.read_gatt_char(SELECT))[0]
        check("current guard reads back", current == last, current)

        serial_lines.clear()
        await c.write_gatt_char(SELECT, bytes([len(names)]), response=True)
        check("invalid pick is rejected", await wait_for(serial_lines, rf"no guard {len(names)} ", 3))
        check("...and the current guard comes back", await wait_for(serial_lines, rf"guard {last} drawn", 5))

        level = (await c.read_gatt_char(BRIGHT))[0]
        check("brightness reads 10-100", 10 <= level <= 100, level)
        serial_lines.clear()
        await c.write_gatt_char(BRIGHT, bytes([level]), response=True)  # same value: nothing changes for the user
        check("brightness write is applied", await wait_for(serial_lines, rf"brightness: {level}%", 3))

        await c.write_gatt_char(SELECT, bytes([0]), response=True)
        await wait_for(serial_lines, r"guard 0 drawn", 3)
        await wait_for(ble_lines, r"guard 0 drawn", 3)
        seen = ble_lines[-1]

    # Same numbers on a reconnect, so the web app can skip lines it already showed.
    async with BleakClient(device) as c:
        again = []
        await c.start_notify(LOG, lambda _, data: again.append(data.decode(errors="replace")))
        same = await wait_for(again, rf"^{re.escape(seen)}$", 3)
        check("a reconnect replays a seen line with its number", same, (seen, again))


asyncio.run(main())
print("all passed" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
