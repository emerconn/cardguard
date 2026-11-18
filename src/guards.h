#pragma once
#include <Arduino_GFX.h>

// A card guard is just a picture drawn on the 240x240 round screen.
// To add one: write a draw function in guards.cpp, add it to GUARDS and its name to GUARD_NAMES.
typedef void (*GuardFn)(Arduino_GFX *gfx);

extern const GuardFn GUARDS[];
extern const int GUARD_COUNT;
extern const char *const GUARD_NAMES[];  // one per guard, sent to the phone page
void drawNoGuard(Arduino_GFX *gfx, int n);  // "NO GUARD n" error screen
