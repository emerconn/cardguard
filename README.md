# cardguard

Poker card guard on an ESP32-C3 1.28" round GC9A01 display (240x240). Multiple display modes, e.g. chip leader.

## Build

Firmware is Rust, bare metal (`esp-hal` + Embassy, BLE via `esp-radio` + `trouble-host`). Needs
[rustup](https://rustup.rs/) (the toolchain and RISC-V target install themselves from
`firmware/rust-toolchain.toml`) and `cargo install espflash --locked`.

| Path | What |
|---|---|
| `firmware/` | The board's program: display, button, backlight, BLE, sleep (`src/main.rs`, `src/display.rs`) |
| `guards/` | Designs and the anti-aliased renderer; `no_std`, so the same code also runs on the PC for previews and tests |

```sh
cd firmware
cargo build --release          # compile -> target/riscv32imc-unknown-none-elf/release/cardguard
cargo run --release            # compile if needed, then flash over USB
espflash monitor               # serial output; Ctrl+C to exit
cd ../guards && cargo test     # renderer tests on the PC
```

- Only one program can hold the serial port: close any running monitor before flashing, or it
  fails with "port is busy".
- The monitor shows `guard N drawn in X ms` after each redraw. Typing `n` steps to the next design.
- If flashing can't connect, hold BOOT and tap RST (or hold BOOT while plugging in), then flash.
- Editors: rust-analyzer needs `check.allTargets = false` (the firmware has no test harness);
  `.nvim.lua` sets it for Neovim (`vim.o.exrc = true`).

Pins are in `firmware/src/main.rs` (vendor pinout, RST not wired). Board specs, pinout, dimensions and sources:
[`docs/hardware.md`](docs/hardware.md).

Preview every guard on the PC with the same renderer the board runs:

```sh
tools/preview/run.sh   # writes preview/guard_N.png
```

Time every guard on the real board (needs the current firmware flashed, no serial monitor open):

```sh
python3 tools/drawtime.py   # resets the board, then sends 'n' over serial to step through guards
```

Tests: `node --test` for the web app; board tests and setup are in [`test/README.md`](test/README.md).

Pick a guard from any Bluetooth LE client: in nRF Connect (iOS/Android) connect to `cardguard`,
open the service `9a49e65d-…` and write to characteristic `6eeb6fbb-…` the guard number as Text
(`2`) or as a byte (`02`). Numbers past the last guard show "NO GUARD n" for 3 s and are logged to serial (only the button wraps). Only while the screen is on: sleep turns Bluetooth off, and waking restarts the board on the same guard. The web app reads the guard names from the board (`GUARD_NAMES`) and highlights the guard on screen: the guard characteristic is also readable and notifies every change (web app, BOOT button, error redraw). Its brightness slider sets the backlight (10-100 %, kept across restarts). While connected the **connect** button reads **disconnect**; a deliberate disconnect clears the controls; a dropped connection (board asleep, reset, out of range) freezes them and reconnects by itself (every 0.3 s for ~2 min while the page is open), and **connect** goes straight back to the same board. Its **debug** toggle (top right) shows the board's log lines live (sent as BLE notifications; lines from before the web app connected only reach USB serial). It keeps advertising while connected (up to 3 connections), so a reloaded web app still finds it; connects, disconnects and picks are logged to serial.

Text uses fonts generated into `guards/src/fonts.rs`. When a design needs a new character, add it to the
lists in `tools/fonts.sh` and run it.

## Hardware budget

ESP32-C3: 4 MB flash, ~320 KB RAM for the program, no FPU, no PSRAM.
espflash's default partition table gives the app ~3.9 MB (no OTA, which we don't use); the firmware
is ~460 KB. Fonts and other data live in flash as `static`, not RAM.

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
| 2026-11-17 | Invalid BLE pick: serial log line + 3 s red "NO GUARD n" screen, then back to the current guard; new shared styles `ALERT`, `BIG_NUMBER` | The client gets no reply, so the screen must show the mistake; the button still wraps since it has no specific input |
| 2026-11-18 | Web app `docs/app` hosted on GitHub Pages by our own workflow (`.github/workflows/pages.yml`), not the built-in Jekyll branch build | Plain HTML needs no build; built-in one used deprecated Node 20 actions; ~20 s deploy, only when `docs/` changes |
| 2026-11-18 | `tools/compiledb.py` pre-build script regenerates `compile_commands.json` when missing or older than `platformio.ini` / the `src/` file list | clangd showed "file not found" after adding NimBLE; now any build fixes it, and unchanged builds pay nothing |
| 2026-11-18 | **No gain**: trimming BLE roles (`CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED` / `_OBSERVER_DISABLED`) | Arduino-ESP32 ships the NimBLE host precompiled with all roles (its `sdkconfig.h` wins); flash was byte-identical. Only a custom framework build could trim it |
| 2026-11-18 | Battery power: backlight PWM (brightness 10-100 %, squared curve, saved in NVS, slider in the web app), CPU 80 MHz, `loop()` idles 10 ms, BLE advert every 0.5-0.7 s | Goal: a few hours on a LiPo. Designs now draw in 211-314 ms (was 112-165), still under 500. Not measured with a meter yet; IP5306 may cut power below ~45 mA load |
| 2026-11-18 | Tests in `test/`: `node --test` (page logic, fake browser, runs on every PR in the `Tests` workflow) and `test/hardware/smoke.py` (board over serial + BLE, run after flashing); `tools/` is for utilities | Web app bugs reached users before; no firmware unit tests (logic is thin glue, the risk is BLE and the screen) |
| 2026-11-18 | `main` is tested and validated: every change goes through a PR, the `Tests` workflow must pass to merge (ruleset), Pages deploys from `main` after merge | Tests belong before main, not as a gate inside the deploy |
| 2026-11-18 | Squash merges only, Conventional Commits for commits and PR titles, CI jobs named lowercase-with-dashes (rules in `CLAUDE.md`) | `main` history reads as one conventional commit per PR |
| 2026-11-18 | Draw-time target **under 200 ms** at CPU 80 MHz. Renderer: shapes visit only each row's covered columns and store fully covered runs straight to the buffer; per-row start values stepped in fixed point instead of soft-float; integer `fsin`/`fcos` (newlib's cost ~40 us each, ~800 calls a frame); 120-row strips (2 scene passes, 58 KB, heap 141 -> 122 KB free) | 212-345 ms -> 108-169 ms. Previews match the old renderer except 4-27 edge pixels per guard, off by one RGB565 step. Not done: full-frame buffer (would leave ~64 KB heap for BLE); SPI push (~23 ms) can't overlap, the bus driver has no DMA |
| 2026-11-18 | Fast adverts (20-30 ms) after boot, wake and a disconnect, slow (0.5-0.7 s) once connected or after 30 s; web app retries every 0.3 s | Measured (`test/hardware/reconnect_time.py`, 5 trials): first advert after waking 1.77 s -> 0.35 s, reconnect 4.49 s -> 1.16 s |
| 2026-11-18 | **No gain**: DMA double-buffered strip push (own IDF SPI device on a shared `Arduino_ESP32SPIDMA` bus, 4 x 60-row strips in 2 x 29 KB, push queued while the next strip draws) | Push cost 23 -> ~10 ms (5 ms is the RGB565 byte swap), but each extra scene pass costs 7-15 ms: 108/145/137/169 -> 108/149/141/185 ms. 80-row strips est. break-even (+19 KB), 2 x 120-row est. -6 ms (+58 KB heap). Fewer scene passes matter, not the push |
| 2026-11-18 | **Firmware rewritten in Rust**: bare-metal `esp-hal` + Embassy, BLE via `esp-radio` + `trouble-host`; renderer and designs in the `no_std` `guards` crate (also runs on the PC for previews) | Memory safety; event-driven (the CPU halts between button, serial and BLE events instead of a 10 ms poll loop); 468 KB flash (was 680). Same BLE UUIDs, byte formats and serial lines, so the web app and tests are unchanged. RAM: 117 KB static + 64 KB heap (35 KB used by BLE) in the bootloader's reclaimed RAM, ~200 KB left for the stack (peak not measured); C++ had 122 KB heap free |
| 2026-11-18 | Rust renderer pixel-identical to the C++ one (previews diffed); float-to-int by reading the bits, `f32` min/max as plain compares, per-pixel helpers force-inlined | Rust's saturating `as` casts and NaN-aware min/max cost 3+ extra soft-float calls each without an FPU: draws were 134/192/196/217 ms, now 93/139/138/167 (C++ 108/145/137/169) |
| 2026-11-18 | Strip buffer stored in panel byte order (big-endian RGB565), pushed by DMA as two 60-row halves | No byte-swap pass and the CPU idles during the push: ~23 -> 13 ms a frame. One SPI DMA transfer is at most 32,736 bytes, a 120-row strip is 57,600 |
| 2026-11-18 | Waking from sleep restarts the chip, guard kept in RTC memory | esp-radio's BLE controller advertises nothing after light sleep and only shuts down when dropped. The panel keeps its image meanwhile. First advert 0.51 s after waking, reconnect 1.24 s (C++ 0.35 / 1.16 s; the gap is mostly the bootloader) |
| 2026-11-18 | Brightness kept as 2 raw bytes (tag + value) at the start of the nvs partition, sector erased per save | No NVS library needed; saves are rare (2 s after the slider stops). The old C++ NVS value isn't read, so the first boot after updating starts at 60 % |

### Renderer vs SVG (renderer chosen)

| | `option/renderer` | `option/svg` |
|---|---|---|
| How | Designs are code; `guards/src/smooth.rs` anti-aliases shapes and rotated text on the chip | Designs are SVG; rendered on the PC, stored palette + RLE (~20 KB each) |
| Looks | Best | Very good |
| Draw time | 112-165 ms (was ~10 s before fixed-point rewrite) | Instant (~30 ms) |
| Next | Further speedup possible (integer row maths), or pre-render its output on the PC | Kept on GitHub as a fallback |
