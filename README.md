# cardguard

Poker card guard on an ESP32-C3 1.28" round GC9A01 display (240x240). Multiple display modes, e.g. chip leader.

## Build

```sh
pio run -t upload -t monitor   # stop any other serial monitor first, it holds the port
```

Pins are in `src/main.cpp` (vendor pinout, RST not wired).
If upload fails, hold BOOT while plugging in.

Preview every guard on the PC, using the real Arduino_GFX drawing code:

```sh
tools/preview/run.sh   # writes preview/guard_N.png
```

## Hardware budget

ESP32-C3: 4 MB flash, ~320 KB RAM for the program, no FPU, no PSRAM.
Default partitions give the app 1.25 MB; `board_build.partitions = huge_app.csv` raises it to 3 MB
(drops OTA, which we don't use). Images live in flash as `const`, not RAM.

## Decisions

Target: **~20 designs**, each a crisp digital (vector-style) design, not a photo.

| Date | Decision | Why |
|---|---|---|
| 2026-11-16 | Photo bitmap (`assets/chip_leader.png`) is a placeholder only | Want a clean digital look, not a picture of a real guard |
| 2026-11-16 | Images stored as 256-colour palette (58 KB) not raw RGB565 (115 KB) | 20 designs must fit in flash |
| 2026-11-16 | **Dropped**: drawing with Arduino_GFX shapes | No anti-aliasing (jagged), letters can't rotate along the curve |
| 2026-11-16 | Candidates: `option/renderer` and `option/svg` branches | See below |

### Open: renderer vs SVG

| | `option/renderer` | `option/svg` |
|---|---|---|
| How | Designs are code; `src/smooth.h` anti-aliases shapes and rotated text on the chip | Designs are SVG; rendered on the PC, stored palette + RLE (~20 KB each) |
| Looks | Best | Very good |
| Draw time | ~10 s (soft float, no FPU): too slow as-is | Instant |
| Next | Speed it up (fixed-point, tighter bounding boxes), or pre-render its output on the PC | Tune the SVG design to match the renderer's |
