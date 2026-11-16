#include <Arduino_GFX_Library.h>
#include "chip_leader.h"

// TODO: placeholder pins. Replace with the seller's pinout before flashing.
#define PIN_SCK  6
#define PIN_MOSI 7
#define PIN_DC   2
#define PIN_CS   10
#define PIN_RST  1
#define PIN_BL   3

Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, PIN_RST, 0 /* rotation */, true /* IPS */);

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  if (!gfx->begin()) Serial.println("gfx->begin() failed");
  drawChipLeader(gfx, {"Emerson", 12500});  // placeholder data
}

void loop() {}
