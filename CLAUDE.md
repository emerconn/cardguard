# cardguard

See README.md for the build, tools, hardware budget and decision log. Record new design or
architecture decisions in the README's Decisions table.

## When the user gives a new design (reference image)

1. Save the reference image to `assets/<design_name>.png` (snake_case), as given. It is kept to
   compare against and improve the design later.
2. Add a scene to `src/guards.cpp` using `src/smooth.h`, reuse the shared helpers (suits, colours,
   `duck`, ...), and append it to `GUARDS`.
3. New characters in text: add them to the lists in `tools/fonts.sh` and run it.
4. Check it with `tools/preview/run.sh` (view `preview/guard_N.png`, scaled up) against the reference.
5. Flash and time it: `pio run -t upload && python3 tools/drawtime.py`. Keep designs well under ~500 ms.
