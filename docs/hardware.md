# Hardware: ESP32-2424S012N-I

The board this project targets, as bought: "ESP32-C3 1.28 Inch IPS Round LCD Development Board 240x240,
Non-Touch Screen, GC9A01" ([Amazon B0H261ZD42](https://www.amazon.com/dp/B0H261ZD42), EC Buying Store).
Maker: Shenzhen Jingcai Intelligent (JCZN), sold as "Sunton" / "CYD" boards too.

Each fact is tagged with its source (see [Sources](#sources)). **Measured** = checked on our board.
Anything not tagged is unknown until measured.

Vendor files kept in [`vendor/`](vendor/) (all board-specific docs and images from the vendor package):

| File | What |
|---|---|
| [outline-front.jpg](vendor/outline-front.jpg) | Front outline drawing with dimensions |
| [layout-back.jpg](vendor/layout-back.jpg) | Labelled render of the back (buttons, switch, connectors) |
| [schematic-v1.0.png](vendor/schematic-v1.0.png) | Board schematic V1.0 |
| [datasheet.pdf](vendor/datasheet.pdf) | Board datasheet / specification (6 pages) |
| [esp32-c3-mini-1-datasheet.pdf](vendor/esp32-c3-mini-1-datasheet.pdf) | Espressif ESP32-C3-MINI-1/1U module datasheet (module 13.2 x 12.5 x 2.4 mm) |
| [getting-started.pdf](vendor/getting-started.pdf) | Vendor Arduino IDE getting-started manual (16 pages, no dimensions) |
| [flash-tool-1..3.png](vendor/flash-tool-1.png) | Vendor screenshots for flashing the demo .bin with Espressif's Windows flash tool |
| [unrelated-esp-rs-c3-board-pinout.jpg](vendor/unrelated-esp-rs-c3-board-pinout.jpg) | **Not our board**: filed under "Schematic" in the package, but it is the pinout of a different ESP32-C3 dev board (ESP-RS) |

Left out of the package: demo sketches and LVGL/LovyanGFX libraries (code and library assets; our pinout
came from `1_2_Factory_samples_without_touch`), Espressif flash-tool manuals, and Windows utilities.

## Variants

| SKU | Touch | Case | |
|---|---|---|---|
| **ESP32-2424S012N-I** | no | no | **ours** |
| ESP32-2424S012C-I | CST816D/S capacitive | no | |
| ESP32-2424S012C-I-Y(W) / (B) | capacitive | white / black shell (~42 mm) | |

[DS], [CNX]. The touch version adds a CST816 controller on GPIO0/1/4/5 [EB]; ours doesn't use those pins.

## Electrical

| | | Source |
|---|---|---|
| MCU | ESP32-C3-MINI-1U module (single-core RISC-V, 160 MHz) | [DS] |
| Memory | 400 KB SRAM, 4 MB flash, no PSRAM, no FPU | [DS], [OH], measured (esptool: "Embedded Flash 4MB") |
| Display | 1.28" round IPS, 240x240, 16-bit colour, GC9A01, COG | [DS], [CD] |
| Power | 5 V USB-C, ~100 mA; or 3.7 V LiPo via JST 1.25 (IP5306 charger/boost) | [DS], [CD] |
| USB | Native USB Serial/JTAG (no UART bridge); D+ GPIO19, D- GPIO18 | [EB] |
| USB-C quirk | No CC resistors (CC1/CC2 unconnected): USB-C to USB-C cables may not power it, use USB-A to USB-C | [OH], [SCH] |
| 5 V input | 1N5819W Schottky diode from USB VBUS | [SCH] |
| 3V3 regulator | DC-DC buck (U4 + inductor L2) per our schematic; [CD] names an A6165P LDO on its (touch) board, likely another revision | [SCH], [CD] |

### Pins

| Function | GPIO | Notes | Source |
|---|---|---|---|
| Display SCK | 6 | | [EB], [HD], `firmware/src/main.rs` |
| Display MOSI | 7 | no MISO | [EB], [HD] |
| Display DC | 2 | | [EB], [HD] |
| Display CS | 10 | | [EB], [HD] |
| Display RST | – | LCD reset tied to the RESET (EN) net, not a GPIO | [EB], [SCH] |
| Backlight | 3 | AO3402 MOSFET (Q2), 3.9 Ω LED resistor, HIGH = on, PWM dimmable | [EB], [CD], [SCH] |
| BOOT button | 9 | 10k pull-up (R2); strapping pin: held at reset = download mode | [CD], [EB], [SCH] |
| RESET button | EN | 10k pull-up (R1) + 0.1 µF; hard reset, not readable by firmware | [CD], [SCH] |
| Spare | 8 | pad S1, pulled up; the only free GPIO | [EB] |
| SH1.0-4P | GND, 3V3, TX (21), RX (20) | JST-SH 1.0 mm serial header | [EB], [CD] |

The display runs at 80 MHz SPI (`firmware/src/main.rs`), measured fine on our panel.

### Power switch (side)

- It is a momentary 4-pin push switch (SW1) wired through R10 to the KEY input of the IP5306 battery
  charger/boost chip, not a GPIO: the firmware can't read it [CD], [SCH].
- Short press turns the battery output on, double press turns it off [CD].
- On USB power it does nothing (observed on our board): USB 5 V feeds the board directly. It only acts
  as an on/off switch when running from a LiPo on the JST 1.25 connector.
- **Unverified (from memory of the IP5306 datasheet, not checked):** the IP5306 shuts its output off by
  itself when the load stays very low for ~30 s. On battery, our long-press sleep (`firmware/src/main.rs`) may
  draw little enough to trigger this, so the board would power off fully instead of sleeping. Test
  with a battery before relying on sleep.

## Mechanical

From the datasheet's "Product Size" drawing [DS] (front view, mm):

| | mm | Source |
|---|---|---|
| PCB | **Ø 37.00** circle | [DS] |
| USB tab | **14.23** wide, **3.74** long from where its straight sides start; reaches **1.5** past the circle's lowest point (38.5 - 37) | [DS] |
| Overall | 37.00 wide x **38.50** tall (circle + tab) | [DS] |
| Visible display area | **Ø 32.4** | [DS] |
| Display glass outer diameter | **Ø 36.70** (measured), ~centred on the PCB, ≤ 0.5 offset | measured |
| Weight | ~20 g | [DS] |
| Case version | ~Ø 42 (vendor shell) | [CNX] |
| Mounting holes | none shown or mentioned | [DS], [CNX] |
| REST / BOOT buttons | TS-4.2x3.2x2.5 tact switches: body 4.2 x 3.2 mm, 2.5 mm tall | [SCH] |
| ESP32-C3-MINI-1U module (back) | 13.2 x 12.5 x 2.4 mm | ESP32-C3-MINI-1 datasheet in the vendor package |
| Battery connector | JST 1.25 mm 2-pin, 90° SMT (PicoBlade-style), flat at ~45°, opening faces the board centre and is open on top, pins on the centre side (observed). Typical clone CJT A1251WR-S-2P: 7.75 overall with solder tabs, body 4.70 deep, **3.20 tall**. Measured total (1a - 1b) gives 4.5 above the PCB back: **unresolved**, check with a depth rod | [CJT](https://static.chipdip.ru/lib/862/DOC012862213.pdf), observed |

Back side (components side), from the datasheet's labelled render [DS], USB tab at the bottom:

| Part | Where (back view) |
|---|---|
| ESP32-C3-MINI-1U module | top centre, against the top edge |
| **REST** (reset) button | top left of the module |
| **BOOT** button | top right of the module |
| **SWITCH** (power, IP5306 KEY, see [Power switch](#power-switch-side)) | right edge, operated **from the side** |
| SH1.0-4P serial header | lower left |
| Battery JST1.25-2P | lower right |
| **USB-C** | in the bottom tab, plug enters from the bottom edge |

Inferred, not stated: the back view is mirrored relative to the front, so with the screen facing you
REST is top right, BOOT top left, and the power switch on the left edge. The buttons look like tact
switches facing out the back. Confirm on the board.

Rough positions estimated from the back render ([back layout](vendor/layout-back.jpg)), scaled by the
37 mm PCB. The render has some perspective (it puts USB-C 3 mm off centre, while the front drawing shows
the tab centred), so treat these as **±1-2 mm** until measured. Back view, from the PCB circle centre,
+x right, +y towards the USB tab:

| Part | x mm | y mm |
|---|---|---|
| REST button | -12.0 (measured) | -9.9 (measured) |
| BOOT button | +12.0 (measured) | -9.9 (measured) |
| Power switch lever (side edge) | +17.4 (edge) | -1.5 |
| SH1.0-4P header | -12.6 | +9.7 |
| JST1.25 battery | +12.2 | +8.1 |
| USB-C | +2.7: not centred, shell flush with the tab's +x side edge and its end (back render; 14.23 tab - 8.8 shell) | +15.5 |

## Measured (calipers, our board)

| # | What | mm |
|---|---|---|
| 1a | Total thickness: screen glass front to highest back part, the JST1.25 battery connector (between USB-C and the power switch) | 7.70 |
| 1b | Screen glass front to bare PCB back | 3.20 |
| 1c | PCB alone (at the USB tab) | 1.20 |
| = | Display stack, glass front to PCB front (1b - 1c) | 2.00 |
| = | Tallest back part above the PCB back (1a - 1b) | 4.50 |
| 2a/2b | Screen glass diameter (round) | 36.70 |
| 2c | Glass offset from PCB centre: not measurable, visually near centred; allow up to ~0.5 (part tolerance) | ≤ 0.5 |
| 3a | REST/BOOT bases, outer edge to outer edge | 27.50 |
| 3b | REST/BOOT bases, inner edge to inner edge | 20.60 |
| = | Button centres apart ((3a + 3b) / 2); each ~12.0 from the PCB centre if symmetric | 24.05 |
| = | Base width left-right ((3a - 3b) / 2) | 3.45 |
| 3d | Button height above the PCB back (plunger top) | 2.50 |
| 3c | USB tab bottom edge to the far (top) edge of a button base | 32.00 |
| = | Button centre from the PCB circle centre, towards the top: 3c - 20.0 (circle centre to tab bottom = 38.5 - 18.5) - ~2.1 (half the ~4.2 base length) | ~9.9 |
| 3e | Plunger oval: 3.0 long, running towards the USB tab (the direction the cable plugs in), x 2.2 left-right | 3.0 x 2.2 |
| 4a | USB-C shell width (sits flush on the PCB back) | 8.80 |
| 4b | USB-C shell top above the PCB back (depth rod) | 3.00 |
| = | USB-C shell spans 3.2 to 6.2 from the screen glass front (1b + 4b); opening centre ~4.7 | |
| 4c | USB-C shell face vs the tab edge | flush |
| 4d | Plug: various cables will be used, so the case gets a ~1 mm wall at the port plus an outer relief pocket ~13 x 7.5 (typical USB-C overmolds are up to ~12.5 x 7, general figure, not measured) | design |
| 5a | Power switch housing: USB tab bottom edge to the housing's far (top) edge | 22.00 |
| 5b | Switch housing length along the edge | 4.60 |
| 5b | Lever: width along the edge x thickness, centred on the housing | ~2.0 x 1.0 |
| 5b | Lever sticks out past the PCB edge: at rest / pressed in | 0.70 / 0.40 |
| 5d | Lever travel (0.7 - 0.4) | 0.30 |
| N2 | Display FPC wrap at the USB tab: sticks out past the PCB edge (case leaves ~0.7 clearance) | ≤ 0.5 |
| L | Overall length, circle edge across to the tab end, including the FPC wrap (datasheet 38.5 is PCB only; +0.5 FPC) | 39.00 |
| N3 | Thickness where the FPC wraps the tab end: same as the rest (1b) | ≈ 3.2 |

Measurements 3c and 5a were taken from the bare PCB tab end (not the FPC), so positions from the PCB circle
centre use circle centre = 20.0 from the tab end.
| 5c | PCB back surface to the top of the lever (assumed measured from the component side) | 2.50 |
| = | Lever centre: 19.7 from the tab bottom edge (0.3 towards the tab from the PCB circle centre); 2.0 above the PCB back = 5.2 from the screen glass front | |
| 6a | SH1.0-4P serial header height above the PCB back | 4.10 |
| 6b | JST1.25 battery connector (mounted at an angle): USB tab bottom edge to its farthest corner | 15.50 |

At the USB tab the screen glass is cut flat and the display's flex cable (FPC) wraps around the board
edge to the back ([photo](photos/front-fpc-notch.png), tab on the left; green tab = screen protector pull
tab). The case wall must follow this flat and leave clearance for the FPC bend, which is fragile.

## Still unmeasured

Everything the first case needs is measured (2026-11-16). Not measured, not needed yet: battery size and
placement (later case version), and exact glass offset (≤ 0.5, absorbed by the front lip overlap).

## Sources

- **[DS]** Jingcai datasheet "1.28 inch ESP32 module ESP32-2424S012N/C" (6 pages, 2023-04-17), copy in
  [`vendor/datasheet.pdf`](vendor/datasheet.pdf); online: https://cdn.espboards.net/boards/cyd-esp32-2424s012/docs/datasheet.pdf
- **[SCH]** Vendor package `1.28inch_ESP32-2424S012.zip` (118 MB: demos, spec, structure diagrams,
  schematic V1.0, ESP32-C3-MINI-1 datasheet, user manual, flash tool). Schematic and drawings copied into
  [`vendor/`](vendor/). Download: vendor file server folder http://pan.jczn1688.com/1/ESP32%20module
  (direct link http://pan.jczn1688.com/directlink/1/ESP32%20module/1.28inch_ESP32-2424S012.zip; commenters
  report it sometimes fails, use the folder). Found via the wiki below.
- **[WIKI]** QSZNTEC wiki "ESP32-C3-1U-1.28 inch screen": https://wiki.icbbuy.com/doku.php?id=developmentboard:esp32-c3-1.28-lcd
  An Arduino IDE setup tutorial (LovyanGFX samples); no mechanical or pin specs of its own. Its
  "DOC&SCH&Software" link and comments lead to the vendor package above.
- **[EB]** espboards.dev board page (pinout, board outline SVG): https://www.espboards.dev/esp32/cyd-esp32-2424s012/
- **[CD]** CircuitDigest teardown (touch version): https://circuitdigest.com/review/inside-the-esp32-2424s012c-development-board-teardown
- **[CNX]** CNX Software article: https://www.cnx-software.com/2024/08/06/arduino-and-lvgl-compatible-esp32-c3-board-features-a-1-28-inch-round-touchscreen-display-fully-housed-in-a-case/
- **[HD]** HomeDing board page: https://homeding.github.io/boards/esp32c3/jczn-esp32-2424s012.htm
- **[OH]** openHASP docs: https://openhasp-docs.pages.dev/0.7.0/hardware/sunton/esp32-2424s012

Other references, not yet used above:
CircuitPython board page https://circuitpython.org/board/sunton_esp32_2424S012/ and community
PlatformIO board definitions https://github.com/rzeldent/platformio-espressif32-sunton.
