# Case

Parametric OpenSCAD case for the ESP32-2424S012N-I (`cardguard_case.scad`). Every board dimension is a
named variable at the top, taken from the caliper measurements in [`docs/hardware.md`](../docs/hardware.md).

Needs OpenSCAD 2025+ (Manifold backend, renders in seconds). Arch: `omarchy pkg aur add openscad-snapshot-appimage`
(the `extra/openscad` 2021.01 package is old and slow).

## v1

- **Shell**: front lip over the screen's dead border (opening Ø 33.4 hides glass offset), board drops in from the back.
  USB opening sized for a plug overmold (11 x 6.6) around the off-centre port, so most cables seat fully. Relief slot for the power-switch lever
  (switch covered in v1, no battery yet).
- **Lid**: friction-fits into the back. Two posts press the board onto the front lip via the ESP32 module can and the
  USB shell. Oval pin holes over REST and BOOT. Fingernail notch on the rim opposite the USB tab to pry it off.
- Outside: ~40.7 x 42.7 mm, 10.8 mm thick.

## Build

```sh
cd case
openscad -D 'part="shell"' -o shell.stl cardguard_case.scad
openscad -D 'part="lid"'   -o lid.stl   cardguard_case.scad
openscad cardguard_case.scad                       # GUI, default part="assembly" (board ghost + cut-away case)
openscad -D 'part="section"' -D section_x=12 ...   # 2D slice through x = 12 (BOOT button) to check fit
```

## Printing

- Shell: front face down on the bed (the lip opening has a 45° chamfer, the USB opening bridges), no supports.
- Lid: flat, posts up.
- 0.2 mm layers, PLA or PETG.

## Tuning after a test print

| Too... | Change |
|---|---|
| Board tight / loose in the pocket | `clear` (0.25 per side) |
| Lid too tight / falls out | `fit` (0.1 per side) |
| Board rattles front-to-back | lower `press_gap` (0.1) to 0 |
| Lid won't seat (posts hit first) | raise `press_gap` |
| Pin holes miss the buttons | `btn_x`, `btn_y` |
