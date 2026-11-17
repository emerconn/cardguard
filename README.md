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

Time every guard on the real board (needs the current firmware flashed, no serial monitor open):

```sh
python3 tools/drawtime.py   # resets the board, then sends 'n' over serial to step through guards
```

Text uses fonts generated into `src/fonts.h`. When a design needs a new character, add it to the
lists in `tools/fonts.sh` and run it.

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
| 2026-11-16 | **Chosen: renderer** (`src/smooth.h`), merged to main; SVG kept on `option/svg` | Best looks, designs are code, 258 ms is fast enough |
| 2026-11-16 | Renderer inner loops are integer / fixed point | No FPU: soft-float per pixel took ~10 s, now 258 ms |
| 2026-11-16 | BOOT: short press = next design (on release), hold 2 s = sleep; light sleep, not deep | Only GPIO0-5 can wake the C3 from deep sleep; BOOT is GPIO9. RST is hard-wired reset, not readable |

### Renderer vs SVG (renderer chosen)

| | `option/renderer` | `option/svg` |
|---|---|---|
| How | Designs are code; `src/smooth.h` anti-aliases shapes and rotated text on the chip | Designs are SVG; rendered on the PC, stored palette + RLE (~20 KB each) |
| Looks | Best | Very good |
| Draw time | 258 ms (was ~10 s before fixed-point rewrite) | Instant (~30 ms) |
| Next | Further speedup possible (integer row maths), or pre-render its output on the PC | Kept on GitHub as a fallback |
