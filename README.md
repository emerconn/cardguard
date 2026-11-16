# cardguard

Poker card guard on an ESP32-C3 1.28" round GC9A01 display (240x240). Multiple display modes, e.g. chip leader.

## Build

```sh
pio run -t upload
pio device monitor
```

Set the display pins in `src/main.cpp` from your board's pinout first.
If upload fails, hold BOOT, tap RST, release BOOT.
