# cardguard

A poker card guard: an ESP32-C3 with a 1.28" round display (GC9A01, 240x240) that shows one of several
designs, picked with its button or over Bluetooth.

| Path | What |
|---|---|
| `firmware/` | The board's program (Rust, bare metal: `esp-hal`, Embassy, BLE) |
| `guards/` | The designs and renderer; also runs on the PC for previews and tests |
| `web/` | Web app to pick a design and set brightness over Bluetooth |
| `tools/` | Previews, draw timing, font generation |

## Setup

Install [rustup](https://rustup.rs/) (the toolchain installs itself) and `cargo install espflash --locked`.

## Build and flash

```sh
cd firmware
cargo run --release    # build and flash over USB
espflash monitor       # serial log; Ctrl+C to exit, type n for the next design
```

- "Port is busy": close the serial monitor first.
- Can't connect: hold BOOT and tap RST, then flash again.
- Neovim: set `vim.o.exrc = true` so `.nvim.lua` configures rust-analyzer.

To flash the latest release, fetch the tags first so the board gets the right version:

```sh
git switch main && git pull --tags
cd firmware && cargo run --release
```

The board logs its version (`cardguard v0.2.0`) on boot.

## Develop

```sh
tools/preview/run.sh          # render every design to preview/guard_N.png
python3 tools/drawtime.py     # time every design on the board (no monitor open)
cd guards && cargo test       # renderer tests
node --test                   # web app tests
```

Board tests: [`test/README.md`](test/README.md). New characters in text: add them to `tools/fonts.sh` and run it.

## Bluetooth

Use the web app, or any BLE client (e.g. nRF Connect): connect to `cardguard` and write the design
number to characteristic `6eeb6fbb-…` as text (`2`) or a byte (`02`). Bluetooth is off while the
screen sleeps.

## Hardware

ESP32-C3: 4 MB flash, ~320 KB RAM, no FPU, no PSRAM. The firmware is ~460 KB; fonts and data live in
flash. Pins are in `firmware/src/main.rs`; specs and pinout in [`docs/hardware.md`](docs/hardware.md).

Design decisions: [`docs/decisions.md`](docs/decisions.md).
