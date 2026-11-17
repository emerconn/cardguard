#include <Arduino_GFX_Library.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
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
  Serial.setTxTimeoutMs(0);  // drop output nobody reads; the default 2 s wait froze the button after ~10 prints
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  pinMode(PIN_BTN, INPUT_PULLUP);
  if (!gfx->begin(80000000)) Serial.println("gfx->begin() failed");  // 80 MHz SPI: library default is 40
  show(0);
}

void waitRelease() {
  while (digitalRead(PIN_BTN) == LOW) delay(10);
  delay(50);  // debounce
}

// "Off": backlight and panel off, chip in light sleep until the button is pressed again.
// Light, not deep, sleep because the C3 can only wake from deep sleep on GPIO0-5 and BOOT is GPIO9.
void sleepUntilPress() {
  digitalWrite(PIN_BL, LOW);
  gfx->displayOff();  // panel sleep keeps its image, so no redraw on wake
  waitRelease();      // otherwise the held button would wake us straight away
  gpio_wakeup_enable((gpio_num_t)PIN_BTN, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
  gpio_wakeup_disable((gpio_num_t)PIN_BTN);
  gfx->displayOn();
  digitalWrite(PIN_BL, HIGH);
  waitRelease();  // the waking press shouldn't also switch designs
}

void loop() {
  // shortcut: blocking button handling, fine until other work runs in loop()
  // Short press: next design (on release). Hold 2 s: sleep.
  if (digitalRead(PIN_BTN) == LOW) {
    unsigned long t = millis();
    while (digitalRead(PIN_BTN) == LOW && millis() - t < 2000) delay(10);
    if (millis() - t >= 2000) {
      sleepUntilPress();
    } else {
      show(current + 1);
      waitRelease();
    }
  }
  if (Serial.available() && Serial.read() == 'n') show(current + 1);  // tools/drawtime.py steps guards this way
  // TODO: wireless switching (small web page) calls show() too.
}
