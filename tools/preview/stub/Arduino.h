// Just enough Arduino for Arduino_GFX's drawing code to build on a PC.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "Print.h"
using std::max;
using std::min;
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))
#define pgm_read_dword(a) (*(const uint32_t *)(a))
#define pgm_read_pointer(a) (*(void *const *)(a))
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define PI 3.1415926535897932384626433832795
#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }
typedef bool boolean;
typedef uint8_t byte;
inline void delay(unsigned long) {}
inline unsigned long millis() { return 0; }
