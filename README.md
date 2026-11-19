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
(`2`) or as a byte (`02`). Numbers past the last guard show "NO GUARD n" for 3 s (a button press ends it early) and are logged to serial (only the button wraps). Only while the screen is on: sleep turns Bluetooth off, and waking restarts the board on the same guard. The web app reads the guard names from the board (`GUARD_NAMES`) and highlights the guard on screen: the guard characteristic is also readable and notifies every change (web app, BOOT button, error redraw). Its brightness slider sets the backlight (10-100 %, kept across restarts). While connected the **connect** button reads **disconnect**; a deliberate disconnect clears the controls; a dropped connection (board asleep, reset, out of range) freezes them and reconnects by itself (every 0.3 s for ~2 min while the page is open), and **connect** goes straight back to the same board. Its **debug** toggle (top right) shows the board's log lines live (sent as BLE notifications; on subscribing it first gets the last 8 lines, so boot lines such as the version and free heap show too). It keeps advertising while connected (up to 3 connections), so a reloaded web app still finds it; connects, disconnects and picks are logged to serial.

Text uses fonts generated into `guards/src/fonts.rs`. When a design needs a new character, add it to the
lists in `tools/fonts.sh` and run it.

## Hardware budget

ESP32-C3: 4 MB flash, ~320 KB RAM for the program, no FPU, no PSRAM.
espflash's default partition table gives the app ~3.9 MB (no OTA, which we don't use); the firmware
is ~460 KB. Fonts and other data live in flash as `static`, not RAM.

## Decisions

See [docs/decisions.md](docs/decisions.md).
