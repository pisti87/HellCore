#pragma once

// Könyvtárfüggetlen DHT11 meghajtó, futásidőben állítható digitális lábbal.
//
// - Nem használ külső DHT könyvtárat (nincs lib_deps függőség).
// - A láb bármikor átállítható / letiltható (setPin / setEnabled).
// - A mérés ~25 ms-ig blokkol; a driver legfeljebb MIN_INTERVAL_MS-onként
//   olvas, közben a legutóbbi érvényes értéket adja vissza.
// - Megszakítást NEM tilt (BLE SoftDevice / WiFi mellett biztonságos);
//   az esetleges időzítési hibát az ellenőrző összeg kiszűri, és újrapróbál.
//
// Bekötés: DATA -> választott digitális láb, VCC -> 3V3, GND -> GND.
// Ajánlott: 4.7k-10k felhúzó ellenállás a DATA és a 3V3 közé
// (a driver bekapcsolja a belső felhúzót is).

#include <Arduino.h>
#include <stdint.h>
#include <math.h>

class Dht11 {
public:
  static const uint32_t MIN_INTERVAL_MS = 2000;   // két olvasás minimális távolsága
  static const uint32_t MAX_AGE_MS      = 60000;  // ennél régebbi érték már nem érvényes
  static const uint8_t  ATTEMPTS        = 2;

  Dht11()
    : _pin(-1), _enabled(false), _have(false), _t(0), _h(0),
      _last_try(0), _last_ok(0), _tried(false) {}

  // ---- konfiguráció ------------------------------------------------------

  void configure(bool enabled, int8_t pin) {
    if (pin != _pin) {
      releaseLine();
      _pin = pin;
      _have = false;
      _tried = false;
    }
    if (enabled != _enabled) {
      _enabled = enabled;
      _have = false;
      _tried = false;
      if (!enabled) releaseLine();
    }
  }

  bool   enabled() const { return _enabled; }
  int8_t pin() const     { return _pin; }
  bool   active() const  { return _enabled && _pin >= 0; }

  // ---- olvasás -----------------------------------------------------------

  // Visszatér: true, ha van friss (MAX_AGE_MS-nél nem régebbi) érvényes érték.
  bool get(float& temperature, float& humidity) {
    if (!active()) return false;

    uint32_t now = millis();
    if (!_tried || (uint32_t)(now - _last_try) >= MIN_INTERVAL_MS) {
      _tried = true;
      _last_try = now;
      float t, h;
      if (readOnce(t, h)) {
        _t = t; _h = h; _have = true;
        _last_ok = millis();
      }
    }

    if (!_have) return false;
    if ((uint32_t)(millis() - _last_ok) > MAX_AGE_MS) {
      _have = false;
      return false;
    }
    temperature = _t;
    humidity = _h;
    return true;
  }

  // ---- tiszta (hardverfüggetlen) függvények, egységteszthez ---------------

  // 5 bájtos nyers keret ellenőrzése és értelmezése.
  static bool decode(const uint8_t d[5], float& temperature, float& humidity) {
    uint8_t sum = (uint8_t)(d[0] + d[1] + d[2] + d[3]);
    if (sum != d[4]) return false;

    float h = (float)d[0] + (float)d[1] * 0.1f;

    float t = (float)(d[2] & 0x7F);
    if (d[2] & 0x80) {
      // egyes gyártmányok az előjelet a 2. bájt 7. bitjén jelzik
      t = -t;
    }
    float frac = (float)(d[3] & 0x0F) * 0.1f;
    t += (d[3] & 0x80) ? -frac : frac;

    // ésszerű tartomány (DHT11: 0..50 C, 20..90 %RH, tűréssel)
    if (h < 0.0f || h > 100.0f) return false;
    if (t < -40.0f || t > 80.0f) return false;
    // csupa nulla keret (nem csatlakoztatott / lebegő láb) elutasítása
    if (d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0) return false;

    temperature = t;
    humidity = h;
    return true;
  }

  // 40 bit impulzushosszából (low / high, µs) bájtokat készít.
  // Bit = 1, ha a magas szint hosszabb, mint az előtte lévő alacsony.
  static void bitsFromPulses(const uint16_t low_us[40], const uint16_t high_us[40], uint8_t out[5]) {
    for (int i = 0; i < 5; i++) out[i] = 0;
    for (int i = 0; i < 40; i++) {
      out[i / 8] <<= 1;
      if (high_us[i] > low_us[i]) out[i / 8] |= 1;
    }
  }

private:
  int8_t   _pin;
  bool     _enabled;
  bool     _have;
  float    _t, _h;
  uint32_t _last_try;
  uint32_t _last_ok;
  bool     _tried;

  void releaseLine() {
    if (_pin >= 0) pinMode(_pin, INPUT);
  }

  // Megvárja, hogy a láb 'level' szintre kerüljön; visszaadja a várakozást µs-ban,
  // vagy 0xFFFF-et időtúllépéskor.
  uint16_t waitFor(uint8_t level, uint16_t timeout_us) {
    uint32_t start = micros();
    while (digitalRead(_pin) != level) {
      if ((uint32_t)(micros() - start) > timeout_us) return 0xFFFF;
    }
    uint32_t d = (uint32_t)(micros() - start);
    return d > 0xFFFE ? 0xFFFE : (uint16_t)d;
  }

  bool readFrame(uint8_t out[5]) {
    // Indítójel: >18 ms alacsony szint, majd rövid magas.
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
    delay(20);
    digitalWrite(_pin, HIGH);
    delayMicroseconds(30);
    pinMode(_pin, INPUT_PULLUP);

    // Válasz: ~80 us alacsony, ~80 us magas.
    if (waitFor(LOW, 200)  == 0xFFFF) return false;
    if (waitFor(HIGH, 200) == 0xFFFF) return false;
    if (waitFor(LOW, 200)  == 0xFFFF) return false;

    uint16_t low_us[40], high_us[40];
    for (int i = 0; i < 40; i++) {
      // Alacsony szint (~50 us) mérése: a lejtőtől a következő emelkedőig.
      uint16_t lo = waitFor(HIGH, 150);
      if (lo == 0xFFFF) return false;
      uint16_t hi = waitFor(LOW, 200);
      if (hi == 0xFFFF) return false;
      low_us[i] = lo;
      high_us[i] = hi;
    }

    bitsFromPulses(low_us, high_us, out);
    return true;
  }

  bool readOnce(float& t, float& h) {
    for (uint8_t attempt = 0; attempt < ATTEMPTS; attempt++) {
      uint8_t frame[5];
      bool ok = readFrame(frame);
      pinMode(_pin, INPUT_PULLUP);   // a vonal nyugalmi állapota: felhúzva
      if (ok && decode(frame, t, h)) return true;
      if (attempt + 1 < ATTEMPTS) delay(50);
    }
    return false;
  }
};
