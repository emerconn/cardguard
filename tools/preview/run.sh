#!/bin/sh
# Render every guard on the PC with the same renderer the board runs and write preview/guard_N.png.
set -e
cd "$(dirname "$0")/../.."
mkdir -p preview
cargo run -q --manifest-path guards/Cargo.toml --example preview
for f in preview/*.ppm; do magick "$f" "${f%.ppm}.png" && rm "$f"; done
