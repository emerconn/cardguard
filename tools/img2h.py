#!/usr/bin/env python3
"""Turn a 240x240 image into a C header: a 256-colour RGB565 palette plus one byte per pixel.

Usage: magick in.png -resize 240x240! -colors 256 rgb:- | python3 tools/img2h.py NAME > src/NAME.h
Draw:  gfx->drawIndexedBitmap(0, 0, (uint8_t *)NAME, (uint16_t *)NAME_palette, 240, 240);
"""
import sys

W = H = 240
name = sys.argv[1]
raw = sys.stdin.buffer.read()
assert len(raw) == W * H * 3, f"expected {W}x{H} RGB, got {len(raw)} bytes"

px = [((raw[i] >> 3) << 11) | ((raw[i + 1] >> 2) << 5) | (raw[i + 2] >> 3) for i in range(0, len(raw), 3)]
palette = sorted(set(px))
assert len(palette) <= 256, f"{len(palette)} colours, quantise with magick -colors 256"
index = {c: i for i, c in enumerate(palette)}

print("#pragma once\n#include <stdint.h>\n")
print(f"const uint16_t {name}_palette[256] = {{")
for i in range(0, len(palette), 16):
    print("  " + ",".join(f"0x{c:04X}" for c in palette[i:i + 16]) + ",")
print("};\n")
print(f"const uint8_t {name}[{W * H}] = {{")
for i in range(0, len(px), 32):
    print("  " + ",".join(str(index[p]) for p in px[i:i + 32]) + ",")
print("};")
