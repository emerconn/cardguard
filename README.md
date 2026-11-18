# cardguard

Poker card guard on an ESP32-C3 1.28" round GC9A01 display (240x240). Multiple display modes, e.g. chip leader.

## Build

Needs [PlatformIO](https://platformio.org/) (`pio`). Run from the repo root; the board is the only
environment (`esp32-c3`), so no `-e` is needed.

```sh
pio run                        # compile firmware -> .pio/build/esp32-c3/firmware.bin
pio run -t upload              # compile if needed, then flash over USB
pio device monitor             # serial output at 115200; Ctrl+C to exit
pio run -t upload -t monitor   # all of the above in one go
```

- Only one program can hold the serial port: close any running monitor before uploading, or the
  upload fails with "port is busy".
- The monitor shows `guard N drawn in X ms` after each redraw. Typing `n` steps to the next design.
- If upload can't connect, hold BOOT and tap RST (or hold BOOT while plugging in), then upload.

Pins are in `src/main.cpp` (vendor pinout, RST not wired). Board specs, pinout, dimensions and sources:
[`docs/hardware.md`](docs/hardware.md).

Preview every guard on the PC, using the real Arduino_GFX drawing code:

```sh
tools/preview/run.sh   # writes preview/guard_N.png
```

Time every guard on the real board (needs the current firmware flashed, no serial monitor open):

```sh
python3 tools/drawtime.py   # resets the board, then sends 'n' over serial to step through guards
```

Pick a guard from a phone over Bluetooth LE: in nRF Connect (iOS/Android) connect to `cardguard`,
open the service `9a49e65d-…` and write to characteristic `6eeb6fbb-…` the guard number as Text
(`2`) or as a byte (`02`). Numbers past the last guard show "NO GUARD n" for 3 s and are logged to serial (only the button wraps). Only while the screen is on; light sleep drops Bluetooth.

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
| 2026-11-16 | Renderer: 3 strips of 80 rows, shapes skip strips before setup maths, integer sqrt / edge maths, `-O2` | Designs went from ~420 ms to 112-165 ms with no visible change (renders compared pixel by pixel) |
| 2026-11-16 | Display SPI at 80 MHz (library default 40) | Halves the 30 ms push; checked on the real panel |
| 2026-11-16 | `Serial.setTxTimeoutMs(0)` | With no serial monitor open, each print blocked ~2 s once the USB buffer filled, freezing the button |
| 2026-11-16 | Shared `TextStyle`s in `guards.cpp`; serif only at large sizes | Small serif text rendered blurry; one set of fonts and sizes keeps designs consistent |
| 2026-11-17 | BLE guard select via NimBLE-Arduino, one write-only characteristic; test with nRF Connect before any app | Lighter than the stock BLE stack (+~250 KB flash total, 657 KB used); same firmware serves a later Bluefy page or iOS app |
| 2026-11-17 | Invalid BLE pick: serial log line + 3 s red "NO GUARD n" screen, then back to the current guard; new shared styles `ALERT`, `BIG_NUMBER` | Phone gets no reply, so the screen must show the mistake; the button still wraps since it has no specific input |

### Renderer vs SVG (renderer chosen)

| | `option/renderer` | `option/svg` |
|---|---|---|
| How | Designs are code; `src/smooth.h` anti-aliases shapes and rotated text on the chip | Designs are SVG; rendered on the PC, stored palette + RLE (~20 KB each) |
| Looks | Best | Very good |
| Draw time | 112-165 ms (was ~10 s before fixed-point rewrite) | Instant (~30 ms) |
| Next | Further speedup possible (integer row maths), or pre-render its output on the PC | Kept on GitHub as a fallback |
