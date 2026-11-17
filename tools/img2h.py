#!/usr/bin/env python3
"""Turn a 240x240 image into a C header of RGB565 pixels.

Usage: magick in.png -resize 240x240! rgb:- | python3 tools/img2h.py NAME > src/NAME.h
"""
import sys

W = H = 240
name = sys.argv[1]
raw = sys.stdin.buffer.read()
assert len(raw) == W * H * 3, f"expected {W}x{H} RGB, got {len(raw)} bytes"

px = [((raw[i] >> 3) << 11) | ((raw[i + 1] >> 2) << 5) | (raw[i + 2] >> 3) for i in range(0, len(raw), 3)]
print("#pragma once\n#include <stdint.h>\n")
print(f"const uint16_t {name}[{W * H}] = {{")
for i in range(0, len(px), 16):
    print("  " + ",".join(f"0x{p:04X}" for p in px[i:i + 16]) + ",")
print("};")
