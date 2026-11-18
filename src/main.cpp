#include <Arduino_GFX_Library.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
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

// Log lines go to USB serial and, as BLE notifications, to the web app's log panel.
// shortcut: lines before a client subscribes (boot, heap) only reach USB serial; keep a backlog if that matters.
NimBLECharacteristic *logChar = nullptr;
NimBLECharacteristic *selectChar = nullptr;  // guard number: written to pick, read/notified to show the current one
void logf(const char *fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.print(buf);
  size_t n = strlen(buf);
  if (n && buf[n - 1] == '\n') n--;  // the page adds its own line breaks
  if (logChar) logChar->notify((const uint8_t *)buf, n);  // this overload leaves the stored value alone: safe from both tasks
}

int current = 0;
volatile int requested = -1;  // set by BLE, drawn in loop(): the BLE callback runs on another task

// Backlight brightness in percent (10-100), set from the web app and kept in flash (NVS).
Preferences prefs;
int brightness = 60;
volatile int reqBright = -1;     // set by BLE, applied in loop()
unsigned long brightChangedAt;   // saved 2 s after the slider stops, not on every drag step (flash wear)
bool brightDirty = false;

// Squared so the slider feels even: LED brightness looks nearly flat across the top half of linear PWM.
void setBacklight(int pct) { ledcWrite(PIN_BL, pct ? max(1, pct * pct * 255 / 10000) : 0); }

// Fast adverts (20-30 ms) after boot, wake and a disconnect, so a reconnecting phone finds the board at
// once; slow (0.5-0.7 s, saves power) as soon as something connects, or after 30 s. Callbacks run on the
// BLE task, so they only request a switch; loop() does it.
enum { ADV_KEEP, ADV_FAST, ADV_SLOW };
volatile int advRequest = ADV_KEEP;
bool fastAdv = false;
unsigned long fastAdvSince;
void setAdverts(bool fast) {
  NimBLEAdvertising *a = NimBLEDevice::getAdvertising();
  a->stop();
  a->setMinInterval(fast ? 32 : 800);  // units of 0.625 ms
  a->setMaxInterval(fast ? 48 : 1120);
  if (!a->start()) logf("ble: advertising failed\n");
  fastAdv = fast;
  fastAdvSince = millis();
  logf("ble: %s adverts\n", fast ? "fast" : "slow");
}

// Keep advertising while connected: a page that lost its handle (Bluefy reload) can still find the
// board, instead of an empty picker while the old connection lingers. NimBLE allows 3 connections.
struct ServerCallbacks : NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override {
    logf("ble: connected %s (%d open)\n", info.getAddress().toString().c_str(), s->getConnectedCount());
    advRequest = ADV_SLOW;  // also restarts advertising, which a connection stops
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &info, int reason) override {
    logf("ble: disconnected %s, reason 0x%x\n", info.getAddress().toString().c_str(), reason);
    advRequest = ADV_FAST;
  }
};

// A client writes a guard number: text "2" (nRF Connect "Text") or a raw byte 0x02 both work.
struct SelectCallbacks : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    std::string v = c->getValue();
    if (v.empty()) return;
    int n = isdigit((unsigned char)v[0]) ? atoi(v.c_str()) : (uint8_t)v[0];
    requested = n;  // checked in loop(): a pick past the end shows an error, only the button wraps
  }
};

// A client writes brightness percent as one raw byte, clamped to 10-100. No text form: bytes 48-57 are the
// digits '0'-'9', so 50 % was read as "2" and dropped to 10 % mid-drag (screen flashed dark).
struct BrightCallbacks : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    std::string v = c->getValue();
    if (v.empty()) return;
    int p = constrain((uint8_t)v[0], 10, 100);
    c->setValue((uint8_t)p);  // reads return the applied value as one byte
    reqBright = p;
  }
};

// Shows on serial whether a client actually subscribed to the log (and is the first line it receives).
struct LogCallbacks : NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &info, uint16_t subValue) override {
    logf("ble: %s %s the log\n", info.getAddress().toString().c_str(), subValue ? "subscribed to" : "unsubscribed from");
  }
};

void show(int i) {
  current = (i + GUARD_COUNT) % GUARD_COUNT;
  unsigned long t = millis();
  GUARDS[current](gfx);
  logf("display: guard %d drawn in %lu ms\n", current, millis() - t);
  // Web app highlights the guard actually on screen: after a reconnect (read) and for BOOT presses (notify).
  if (selectChar) {
    selectChar->setValue((uint8_t)current);
    selectChar->notify();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // drop output nobody reads; the default 2 s wait froze the button after ~10 prints
  setCpuFrequencyMhz(80);    // power: ~2x draw time, still well under 500 ms; BLE needs >= 80
  prefs.begin("cardguard");
  brightness = prefs.getUChar("bright", brightness);
  ledcAttach(PIN_BL, 20000, 8);  // 20 kHz PWM: above hearing
  setBacklight(brightness);
  pinMode(PIN_BTN, INPUT_PULLUP);
  if (!gfx->begin(80000000)) logf("display: begin failed\n");  // 80 MHz SPI: library default is 40
  show(0);

  NimBLEDevice::init("cardguard");
  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  server->advertiseOnDisconnect(true);  // NimBLE 2.x default is false: one connection and it went silent
  NimBLEService *svc = server->createService("9a49e65d-c7e4-4d64-a03d-061e26e9b26a");
  selectChar = svc->createCharacteristic("6eeb6fbb-9458-47d6-9379-e73cab7dfa04",
                                         NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  selectChar->setCallbacks(new SelectCallbacks());
  selectChar->setValue((uint8_t)current);
  // Guard names, one per line, so the web app builds its buttons from the board.
  std::string names;
  for (int i = 0; i < GUARD_COUNT; i++) names += std::string(i ? "\n" : "") + GUARD_NAMES[i];
  svc->createCharacteristic("ab3ba50e-45cb-4145-ae7e-3066f7e872a8", NIMBLE_PROPERTY::READ)->setValue(names);
  NimBLECharacteristic *bright = svc->createCharacteristic("38c7816a-72f6-40eb-b1ab-b01bb0136afc",
                                                            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  bright->setValue((uint8_t)brightness);
  bright->setCallbacks(new BrightCallbacks());
  logChar = svc->createCharacteristic("7c0bd792-ba8a-4791-be95-3c354c4c2fd2", NIMBLE_PROPERTY::NOTIFY);
  logChar->setCallbacks(new LogCallbacks());
  // Name only: name + the 128-bit service UUID overflow the 31-byte advert and it never starts.
  NimBLEDevice::getAdvertising()->setName("cardguard");
  setAdverts(true);
  // After display + BLE are up, so it shows what designs and features actually leave free.
  logf("heap: %u KB free, %u KB largest block, %u KB total\n", ESP.getFreeHeap() / 1024,
                ESP.getMaxAllocHeap() / 1024, ESP.getHeapSize() / 1024);
}

void waitRelease() {
  while (digitalRead(PIN_BTN) == LOW) delay(10);
  delay(50);  // debounce
}

// "Off": backlight and panel off, chip in light sleep until the button is pressed again.
// Light, not deep, sleep because the C3 can only wake from deep sleep on GPIO0-5 and BOOT is GPIO9.
// testSeconds > 0 (serial 'z', for timing reconnects from a PC): also wake by timer, no button needed.
void sleepUntilPress(int testSeconds = 0) {
  logf("power: sleep\n");
  setBacklight(0);
  gfx->displayOff();  // panel sleep keeps its image, so no redraw on wake
  waitRelease();      // otherwise the held button would wake us straight away
  gpio_wakeup_enable((gpio_num_t)PIN_BTN, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  if (testSeconds) esp_sleep_enable_timer_wakeup(testSeconds * 1000000ULL);
  esp_light_sleep_start();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  gpio_wakeup_disable((gpio_num_t)PIN_BTN);
  gfx->displayOn();
  setBacklight(brightness);
  logf("power: woke\n");
  setAdverts(true);
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
  int c = Serial.available() ? Serial.read() : -1;
  if (c == 'n') show(current + 1);    // tools/drawtime.py steps guards this way
  if (c == 'z') sleepUntilPress(5);  // test/hardware/reconnect_time.py: sleep 5 s, wake by timer
  // Take-and-clear in one step: a BLE write landing between a read and a separate "= -1" was lost.
  int n = __atomic_exchange_n(&requested, -1, __ATOMIC_SEQ_CST);
  if (n >= 0) {
    if (n < GUARD_COUNT) {
      logf("ble: picked guard %d\n", n);
      show(n);
    } else {
      logf("ble: no guard %d (valid 0-%d), keeping guard %d\n", n, GUARD_COUNT - 1, current);
      drawNoGuard(gfx, n);
      delay(3000);  // shortcut: blocks the button for 3 s, fine for a rare error
      show(current);
    }
  }
  int b = __atomic_exchange_n(&reqBright, -1, __ATOMIC_SEQ_CST);
  if (b >= 0) {
    brightness = b;
    setBacklight(brightness);
    logf("brightness: %d%%\n", brightness);
    brightDirty = true;
    brightChangedAt = millis();
  }
  if (brightDirty && millis() - brightChangedAt > 2000) {
    prefs.putUChar("bright", brightness);
    brightDirty = false;
    logf("brightness: %d%% saved\n", brightness);
  }
  int adv = __atomic_exchange_n(&advRequest, ADV_KEEP, __ATOMIC_SEQ_CST);
  if (adv != ADV_KEEP) setAdverts(adv == ADV_FAST);
  else if (fastAdv && millis() - fastAdvSince > 30000) setAdverts(false);
  delay(10);  // power: let the CPU idle between checks instead of spinning flat out
}
