# cardguard

See README.md for the build, tools, hardware budget and decision log. Record new design or
architecture decisions in the README's Decisions table.

## Git (GitHub enforces PRs, squash merges and the `web` and `pr-title` checks; these it can't)

- Firmware changes: run `test/hardware/smoke.py` after flashing (CI has no board); say so in the PR.
- CI job names: lowercase-dash job ids. Rename a required job in the `main` ruleset before merging.

## When the user gives a new design (reference image)

1. Save the reference image to `assets/<design_name>.png` (snake_case), as given. It is kept to
   compare against and improve the design later.
2. Add a scene to `guards/src/lib.rs` using `guards/src/smooth.rs`, reuse the shared helpers (suits, colours,
   `duck`, ...), and append it to `GUARDS` and its name to `GUARD_NAMES` (the web app reads them from the board). Text uses only the shared `TextStyle`s (`BAND`,
   `BAND_LONG`, `HEADLINE`, `LABEL`, `CARD_INDEX`, `CARD_RANK`, `ALERT`, `BIG_NUMBER`), never an ad-hoc font or scale;
   small serif text goes blurry.
3. New characters in text: add them to the lists in `tools/fonts.sh` and run it.
4. Check it with `tools/preview/run.sh` (view `preview/guard_N.png`, scaled up) against the reference.
5. Flash and time it: `(cd firmware && cargo run --release) && python3 tools/drawtime.py`. Keep every design under 200 ms.
   Renderer changes: previews must stay pixel-identical unless the change is meant to alter them (diff with `magick compare -metric AE`).

## Case (`case/`)

The STLs are committed so the Windows slicer (Creality Print) can pull them. Whenever
`case/cardguard_case.scad` changes, regenerate both `case/shell.stl` and `case/lid.stl` and commit them in
the same commit, so every print maps to a design version.
