#include <Arduino_GFX_Library.h>
#include "guards.h"

// Pins from the vendor's Factory_samples_without_touch sketch (ESP32-2424S012). RST is not wired.
#define PIN_SCK  6
#define PIN_MOSI 7
#define PIN_DC   2
#define PIN_CS   10
#define PIN_BL   3
#define PIN_BTN  9  // BOOT button on most ESP32-C3 boards; wire an external one if needed

Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, GFX_NOT_DEFINED, 0 /* rotation */, true /* IPS */);

int current = 0;

void show(int i) {
  current = (i + GUARD_COUNT) % GUARD_COUNT;
  unsigned long t = millis();
  GUARDS[current](gfx);
  Serial.printf("guard %d drawn in %lu ms\n", current, millis() - t);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  pinMode(PIN_BTN, INPUT_PULLUP);
  if (!gfx->begin()) Serial.println("gfx->begin() failed");
  show(0);
}

void loop() {
  // shortcut: blocking debounce, fine until other work runs in loop()
  if (digitalRead(PIN_BTN) == LOW) {
    show(current + 1);
    delay(300);
    while (digitalRead(PIN_BTN) == LOW) delay(10);
  }
  if (Serial.available() && Serial.read() == 'n') show(current + 1);  // tools/drawtime.py steps guards this way
  // TODO: wireless switching (small web page) calls show() too.
}
