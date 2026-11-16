#pragma once
#include <Arduino_GFX_Library.h>

// Chip leader mode: shows the current chip leader's name and stack.
struct ChipLeader {
  const char *name;
  int chips;
};

void drawChipLeader(Arduino_GFX *gfx, const ChipLeader &leader);
