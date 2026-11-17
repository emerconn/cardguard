# Case

Parametric OpenSCAD case for the ESP32-2424S012N-I (`cardguard_case.scad`). Every board dimension is a
named variable at the top, taken from the caliper measurements in [`docs/hardware.md`](../docs/hardware.md).

Needs OpenSCAD 2025+ (Manifold backend, renders in seconds). Arch: `omarchy pkg aur add openscad-snapshot-appimage`
(the `extra/openscad` 2021.01 package is old and slow).

## v1

- **Shell**: front lip over the screen's dead border (opening Ø 33.4 hides glass offset), board drops in from the back.
  USB opening port-sized (9.2 x 3.0) around the off-centre port, so no PCB shows; only slim cables seat fully
  (the plug's housing can't enter, the port face is 2.35 behind the case face). Relief slot for the power-switch lever
  (switch covered in v1, no battery yet).
- **Lid**: friction-fits into the back; a flange round its edge rests on a stepped ledge in the shell rim, so it
  cannot tip in when pressed near the edge. Two posts press the board onto the front lip via the ESP32 module can and the
  USB shell. REST and BOOT (the firmware's button) each get a flex tab cut into the lid: a thinned PLA tongue with
  a peg under its tip, pressed with a finger.
- Outside: ~40.7 x 42.7 mm, 11.2 mm thick.

## Build

`shell.stl` and `lid.stl` are committed (for the slicer on another machine). Regenerate both whenever the
`.scad` changes, in the same commit.

```sh
cd case
openscad -D 'part="shell"' -o shell.stl cardguard_case.scad
openscad -D 'part="lid"'   -o lid.stl   cardguard_case.scad
openscad cardguard_case.scad                       # GUI, default part="assembly" (board ghost + cut-away case)
openscad -D 'part="section"' -D section_x=12 ...   # 2D slice through x = 12 (BOOT button) to check fit
```

## Printing

- Shell: front face down on the bed (the lip opening has a 45° chamfer, the USB opening bridges), no supports.
- Lid: `lid.stl` is already in print orientation (flat, posts up). Load as is; never use the slicer's Mirror
  (it swaps the USB post to the wrong side), only rotate.
- 0.2 mm layers, PLA or PETG.

## Tuning after a test print

| Too... | Change |
|---|---|
| Board tight / loose in the pocket | `clear` (0.25 per side) |
| Lid too tight / falls out | `fit` (0.05 per side; 0.1 was loose on the first print) |
| Board rattles front-to-back | lower `press_gap` (0.3; 0.1 and 0.2 made the lid sit on the posts), `usb_press_gap` for the USB post alone (0.2) |
| Lid won't seat (posts hit first) | raise `press_gap` |
| Flex pegs miss the buttons | `btn_x`, `btn_y` |
| Button tabs too stiff / too soft | `flex_t` (1.0), `flex` length (7; 8 worked, below ~6 PLA may crack) |
| Button tabs must be pushed far / clicks by itself | lower / raise `flex_gap` (0.4; keep above `press_gap`) |
