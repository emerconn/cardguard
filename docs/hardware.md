# Hardware: ESP32-2424S012N-I

The board this project targets, as bought: "ESP32-C3 1.28 Inch IPS Round LCD Development Board 240x240,
Non-Touch Screen, GC9A01" ([Amazon B0H261ZD42](https://www.amazon.com/dp/B0H261ZD42), EC Buying Store).
Maker: Shenzhen Jingcai Intelligent (JCZN), sold as "Sunton" / "CYD" boards too.

Each fact is tagged with its source (see [Sources](#sources)). **Measured** = checked on our board.
Anything not tagged is unknown until measured.

Vendor files kept in [`vendor/`](vendor/): [front outline drawing](vendor/outline-front.jpg),
[back layout](vendor/layout-back.jpg), [schematic V1.0](vendor/schematic-v1.0.png),
[datasheet](vendor/datasheet.pdf).

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
| Display SCK | 6 | | [EB], [HD], `src/main.cpp` |
| Display MOSI | 7 | no MISO | [EB], [HD] |
| Display DC | 2 | | [EB], [HD] |
| Display CS | 10 | | [EB], [HD] |
| Display RST | – | LCD reset tied to the RESET (EN) net, not a GPIO | [EB], [SCH] |
| Backlight | 3 | AO3402 MOSFET (Q2), 3.9 Ω LED resistor, HIGH = on, PWM dimmable | [EB], [CD], [SCH] |
| BOOT button | 9 | 10k pull-up (R2); strapping pin: held at reset = download mode | [CD], [EB], [SCH] |
| RESET button | EN | 10k pull-up (R1) + 0.1 µF; hard reset, not readable by firmware | [CD], [SCH] |
| Spare | 8 | pad S1, pulled up; the only free GPIO | [EB] |
| SH1.0-4P | GND, 3V3, TX (21), RX (20) | JST-SH 1.0 mm serial header | [EB], [CD] |

The display runs at 80 MHz SPI (`src/main.cpp`), measured fine on our panel.

### Power switch (side)

- It is a momentary 4-pin push switch (SW1) wired through R10 to the KEY input of the IP5306 battery
  charger/boost chip, not a GPIO: the firmware can't read it [CD], [SCH].
- Short press turns the battery output on, double press turns it off [CD].
- On USB power it does nothing (observed on our board): USB 5 V feeds the board directly. It only acts
  as an on/off switch when running from a LiPo on the JST 1.25 connector.
- **Unverified (from memory of the IP5306 datasheet, not checked):** the IP5306 shuts its output off by
  itself when the load stays very low for ~30 s. On battery, our long-press sleep (`src/main.cpp`) may
  draw little enough to trigger this, so the board would power off fully instead of sleeping. Test
  with a battery before relying on sleep.

## Mechanical

From the datasheet's "Product Size" drawing [DS] (front view, mm):

| | mm | Source |
|---|---|---|
| PCB | **Ø 37.00** circle | [DS] |
| USB tab | **14.23** wide, sticks out **3.74** below the circle (bottom) | [DS] |
| Overall | 37.00 wide x **38.50** tall (circle + tab) | [DS] |
| Visible display area | **Ø 32.4** | [DS] |
| Display glass outer diameter | ? (drawing shows it slightly smaller than the PCB) | to measure |
| Weight | ~20 g | [DS] |
| Case version | ~Ø 42 (vendor shell) | [CNX] |
| Mounting holes | none shown or mentioned | [DS], [CNX] |
| REST / BOOT buttons | TS-4.2x3.2x2.5 tact switches: body 4.2 x 3.2 mm, 2.5 mm tall | [SCH] |

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
| REST button | -12 | -11 |
| BOOT button | +12 | -11 |
| Power switch lever (side edge) | +17.4 (edge) | -1.5 |
| SH1.0-4P header | -12.6 | +9.7 |
| JST1.25 battery | +12.2 | +8.1 |
| USB-C | 0 (centred on tab, per [DS]) | +15.5 |

## To measure (calipers) before designing the case

Not published anywhere; needed for a case that fits:

1. Total thickness: display glass front to tallest part on the back (probably the module or USB-C).
2. PCB thickness, and display glass thickness (glass front to PCB front).
3. Display glass outer diameter, and whether it is centred on the PCB circle.
4. REST and BOOT button centres: distance from the PCB centre (x, y), button cap size, and height above the PCB.
5. Power switch: position along the edge (angle or distance from the USB tab), lever size, how far it sticks out, travel.
6. USB-C: height of the port centre above the PCB back, opening width x height, how far it sits from the tab edge.
7. Clearance for the USB plug's overmold (measure the cable you use).
8. Whether a battery will be fitted (adds depth; JST 1.25 connector height on the back).

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
