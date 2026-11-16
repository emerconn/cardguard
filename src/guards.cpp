#include "guards.h"

static void chipLeader(Arduino_GFX *gfx) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->drawCircle(120, 120, 115, RGB565_YELLOW);
  gfx->drawCircle(120, 120, 112, RGB565_YELLOW);
  gfx->setTextColor(RGB565_YELLOW);
  gfx->setTextSize(3);
  gfx->setCursor(30, 105);
  gfx->print("CHIP LEADER");
  // TODO: real artwork (crown, chip stack).
}

const GuardFn GUARDS[] = {chipLeader};
const int GUARD_COUNT = sizeof(GUARDS) / sizeof(GUARDS[0]);
