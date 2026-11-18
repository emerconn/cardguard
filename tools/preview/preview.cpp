// Renders every guard in src/guards.cpp to a PNG on the PC, using the real Arduino_GFX drawing code.
// Usage: tools/preview/run.sh   -> writes preview/guard_N.png
#include <Arduino_GFX.h>
#include "../../src/guards.h"
#include "../../src/smooth.h"
#include <cassert>

struct Canvas : Arduino_GFX {
  uint16_t px[240 * 240] = {};
  Canvas() : Arduino_GFX(240, 240) {}
  bool begin(int32_t) override { return true; }
  void writePixelPreclipped(int16_t x, int16_t y, uint16_t c) override { px[y * 240 + x] = c; }
};

static void save(const Canvas &c, const char *name) {
  FILE *f = fopen(name, "wb");
  fprintf(f, "P6 240 240 255\n");
  for (uint16_t p : c.px) {
    uint8_t rgb[3] = {uint8_t((p >> 11) << 3), uint8_t(((p >> 5) & 63) << 2), uint8_t((p & 31) << 3)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

int main() {
  for (float a = -20; a < 20; a += 0.0007f)  // the renderer's integer sin/cos stay float-accurate
    assert(fabs(fsin(a) - sin(a)) < 1e-6 && fabs(fcos(a) - cos(a)) < 1e-6);
  for (int g = 0; g < GUARD_COUNT; g++) {
    Canvas c;
    GUARDS[g](&c);
    char name[64];
    snprintf(name, sizeof name, "preview/guard_%d.ppm", g);
    save(c, name);
  }
  Canvas c;
  drawNoGuard(&c, 5);
  save(c, "preview/no_guard.ppm");
}
