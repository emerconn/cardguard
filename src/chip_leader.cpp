#include "chip_leader.h"

void drawChipLeader(Arduino_GFX *gfx, const ChipLeader &leader) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_YELLOW);
  gfx->setTextSize(2);
  gfx->setCursor(48, 70);
  gfx->print("CHIP LEADER");

  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(3);
  gfx->setCursor(40, 110);
  gfx->print(leader.name);

  gfx->setTextSize(4);
  gfx->setCursor(40, 150);
  gfx->print(leader.chips);
  // TODO: get the name/chips from input (buttons, wifi, or BLE) instead of hardcoding.
}
