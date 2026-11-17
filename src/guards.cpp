#include "guards.h"
#include "chip_leader_img.h"  // generated from assets/chip_leader.png by tools/img2h.py

static void chipLeader(Arduino_GFX *gfx) {
  gfx->draw16bitRGBBitmap(0, 0, chip_leader_img, 240, 240);
}

const GuardFn GUARDS[] = {chipLeader};
const int GUARD_COUNT = sizeof(GUARDS) / sizeof(GUARDS[0]);
