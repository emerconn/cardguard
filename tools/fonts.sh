#!/bin/sh
# Regenerate src/fonts.h. Add any new characters a design uses to the lists below.
# Fonts are stored at 4x and drawn by src/smooth.h at scale 0.25 (or smaller) for smooth edges.
set -e
cd "$(dirname "$0")/.."
F=/usr/share/fonts/liberation
{
  echo '#pragma once'
  echo '#include <Arduino_GFX.h>'
  echo
  python3 tools/ttf2gfx.py $F/LiberationSans-Bold.ttf 72 TitleFont4x " THECIPLADRUKQSMNO'VWF72JG0134569"
  python3 tools/ttf2gfx.py $F/LiberationSerif-Bold.ttf 108 PokerFont4x " POKERHLD'EMCT2sIVJANGF"
} > src/fonts.h
