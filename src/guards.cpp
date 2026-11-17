#include "guards.h"
#include "chip_leader_img.h"  // generated from assets/chip_leader.png by tools/img2h.py

// Library takes non-const pointers but only reads; the casts keep the images in flash.
static void chipLeader(Arduino_GFX *gfx) {
  gfx->drawIndexedBitmap(0, 0, (uint8_t *)chip_leader_img, (uint16_t *)chip_leader_img_palette, 240, 240);
}

const GuardFn GUARDS[] = {chipLeader};
const int GUARD_COUNT = sizeof(GUARDS) / sizeof(GUARDS[0]);
