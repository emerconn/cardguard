#!/bin/sh
# Build the guards against the real GFX library on the PC and write preview/guard_N.png.
set -e
cd "$(dirname "$0")/../.."
L=".pio/libdeps/esp32-c3/GFX Library for Arduino/src"
[ -d "$L" ] || pio pkg install
mkdir -p preview
g++ -std=gnu++17 -O1 -w -Itools/preview/stub -I"$L" -o preview/render \
  tools/preview/preview.cpp src/guards.cpp "$L/Arduino_GFX.cpp" "$L/Arduino_G.cpp"
./preview/render
for f in preview/*.ppm; do magick "$f" "${f%.ppm}.png" && rm "$f"; done
