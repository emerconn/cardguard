#pragma once
#include <Arduino_GFX.h>

// A card guard is just a picture drawn on the 240x240 round screen.
// To add one: write a draw function in guards.cpp and add it to GUARDS.
typedef void (*GuardFn)(Arduino_GFX *gfx);

extern const GuardFn GUARDS[];
extern const int GUARD_COUNT;
void drawNoGuard(Arduino_GFX *gfx, int n);  // "NO GUARD n" error screen
