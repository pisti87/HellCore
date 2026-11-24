#pragma once

// Könyvtárfüggetlen DS18B20 (1-Wire) hőmérő meghajtó, futásidőben állítható lábbal.
//
// - Nem használ külső OneWire / DallasTemperature könyvtárat (nincs lib_deps).
// - Egy buszon több érzékelő (legfeljebb HC_DS18B20_MAX_DEVICES), ROM-kereséssel.
//   Támogatott családok: DS18B20 (0x28), DS1822 (0x22), DS18S20 (0x10), MAX31850 (0x3B).
// - NEM blokkoló: a loop() minden hívásakor legfeljebb egy rövid 1-Wire
//   tranzakciót végez (kb. 1-12 ms), a 750 ms-os konverzió alatt nem vár.
// - Csak a néhány µs-os, időzítés-kritikus részeknél tilt megszakítást
//   (BLE SoftDevice / WiFi mellett is biztonságos); a hibás olvasást a CRC kiszűri.
//
// Bekötés: DATA -> választott digitális láb, VDD -> 3V3, GND -> GND,
// 4.7k felhúzó ellenállás a DATA és a 3V3 közé (kötelező).
// Parazita (2 vezetékes) táplálás NEM támogatott.

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <helpers/HwBoards.h>

class Ds18b20 {
public:
  static const uint8_t  MAX_DEVICES = HC_DS18B20_MAX_DEVICES;
  static const uint32_t PERIOD_MS   = 5000;    // mérési ciklus
  static const uint32_t CONVERT_MS  = 800;     // 12 bites konverzió (max. 750 ms) + tartalék
  static const uint32_t MAX_AGE_MS  = 60000;   // ennél régebbi érték már nem érvényes
  static const uint32_t RESCAN_MS   = 300000;  // eszközkeresés ismétlése (új érzékelő felismerése)
  static const uint32_t EMPTY_RESCAN_MS = 10000; // ha nincs eszköz: ilyen gyakran keres

  Ds18b20() : _pin(-1), _enabled(false) { resetAll(); }

  // ---- konfiguráció ------------------------------------------------------

  void configure(bool enabled, int8_t pin) {
    if (pin != _pin) {
      releaseLine();
      _pin = pin;
      resetAll();
      initLine();
    }
    if (enabled != _enabled) {
      _enabled = enabled;
      resetAll();
      if (enabled) initLine(); else releaseLine();
    }
  }

  bool   enabled() const { return _enabled; }
  int8_t pin() const     { return _pin; }
  bool   active() const  { return _enabled && _pin >= 0; }

  // ---- futás -------------------------------------------------------------

  // Gyakran hívandó (fő ciklus). Egy hívás legfeljebb egy rövid tranzakció.
  void loop() {
    if (!active()) return;
    uint32_t now = millis();

    switch (_state) {
      case ST_IDLE: {
        if (_started && (uint32_t)(now - _cycle_at) < PERIOD_MS) return;
        _started = true;
        _cycle_at = now;

        uint32_t rescan = _n == 0 ? EMPTY_RESCAN_MS : RESCAN_MS;
        if (!_scanned || _need_rescan || (uint32_t)(now - _scan_at) >= rescan) {
          startScan();
          return;
        }
        startConversion(now);
        return;
      }

      case ST_SCANNING:
        scanStep(now);
        return;

      case ST_CONVERTING:
        if ((uint32_t)(now - _conv_at) >= CONVERT_MS) {
          _state = ST_READING;
          _read_idx = 0;
        }
        return;

      case ST_READING:
        if (_read_idx < _n) {
          readDevice(_read_idx, now);
          _read_idx++;
        }
        if (_read_idx >= _n) _state = ST_IDLE;
        return;
    }
  }

  // Megtalált érzékelők száma (érvényes értéktől függetlenül).
  uint8_t count() const { return active() ? _n : 0; }

  // Friss (MAX_AGE_MS-nél nem régebbi), CRC-vel ellenőrzött érték.
  bool get(uint8_t idx, float& temperature) const {
    if (!active() || idx >= _n || !_have[idx]) return false;
    if ((uint32_t)(millis() - _ok_at[idx]) > MAX_AGE_MS) return false;
    temperature = _t[idx];
    return true;
  }

  // Az érzékelő 64 bites ROM-azonosítója (pl. a kijelzőn rövid névhez).
  bool rom(uint8_t idx, uint8_t out[8]) const {
    if (idx >= _n) return false;
    memcpy(out, _rom[idx], 8);
    return true;
  }

  // ---- tiszta (hardverfüggetlen) függvények, egységteszthez ---------------

  // Dallas/Maxim CRC-8 (x^8 + x^5 + x^4 + 1, LSB első).
  static uint8_t crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0;
    while (len--) {
      uint8_t inbyte = *data++;
      for (uint8_t i = 8; i; i--) {
        uint8_t mix = (crc ^ inbyte) & 0x01;
        crc >>= 1;
        if (mix) crc ^= 0x8C;
        inbyte >>= 1;
      }
    }
    return crc;
  }

  static bool familySupported(uint8_t family) {
    return family == 0x28 || family == 0x22 || family == 0x10 || family == 0x3B;
  }

  // 9 bájtos scratchpad ellenőrzése és értelmezése.
  // family: a ROM első bájtja (0 = ismeretlen, DS18B20-ként kezeli).
  static bool decodeScratchpad(const uint8_t sp[9], uint8_t family, float& temperature) {
    bool all_zero = true, all_ff = true;
    for (int i = 0; i < 9; i++) {
      if (sp[i] != 0x00) all_zero = false;
      if (sp[i] != 0xFF) all_ff = false;
    }
    if (all_zero || all_ff) return false;           // nincs eszköz / rövidzár
    if (crc8(sp, 8) != sp[8]) return false;

    int16_t raw = (int16_t)(((uint16_t)sp[1] << 8) | sp[0]);
    float t;

    if (family == 0x10) {
      // DS18S20: 0.5 °C felbontás, a COUNT_REMAIN-nel finomítva
      if (sp[7] == 0x10) {
        t = (float)(raw >> 1) - 0.25f + (float)(sp[7] - sp[6]) / (float)sp[7];
      } else {
        t = (float)raw * 0.5f;
      }
    } else {
      if (family == 0x3B) {
        raw &= ~0x0003;                             // MAX31850: alsó 2 bit állapotjelző
      } else {
        switch ((sp[4] >> 5) & 0x03) {              // felbontás a konfigurációs regiszterből
          case 0: raw &= ~0x0007; break;            // 9 bit
          case 1: raw &= ~0x0003; break;            // 10 bit
          case 2: raw &= ~0x0001; break;            // 11 bit
          default: break;                           // 12 bit
        }
      }
      t = (float)raw / 16.0f;
    }

    if (t < -55.0f || t > 125.0f) return false;
    temperature = t;
    return true;
  }

  // A bekapcsolás utáni alapérték (+85.0 °C) – konverzió nélkül ezt adja vissza.
  static bool isPowerOnValue(const uint8_t sp[9], uint8_t family) {
    if (family == 0x10) return sp[0] == 0xAA && sp[1] == 0x00;
    return sp[0] == 0x50 && sp[1] == 0x05;
  }

private:
  enum State : uint8_t { ST_IDLE, ST_SCANNING, ST_CONVERTING, ST_READING };

  int8_t   _pin;
  bool     _enabled;

  State    _state;
  bool     _started;
  bool     _scanned;
  bool     _need_rescan;
  uint32_t _cycle_at, _scan_at, _conv_at;
  uint8_t  _read_idx;

  uint8_t  _n;
  uint8_t  _rom[MAX_DEVICES][8];
  float    _t[MAX_DEVICES];
  uint32_t _ok_at[MAX_DEVICES];
  bool     _have[MAX_DEVICES];
  uint8_t  _fail[MAX_DEVICES];

  // keresés (Maxim AN187) állapota – lépésenként, hívásonként egy eszköz
  uint8_t  _s_rom[8];
  uint8_t  _s_last_discrepancy;
  bool     _s_last_device;
  uint8_t  _s_n;
  uint8_t  _s_list[MAX_DEVICES][8];

#if defined(ESP32)
  static portMUX_TYPE* mux() {
    static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
    return &m;
  }
#endif

  void resetAll() {
    _state = ST_IDLE;
    _started = false;
    _scanned = false;
    _need_rescan = false;
    _cycle_at = _scan_at = _conv_at = 0;
    _read_idx = 0;
    _n = 0;
    memset(_rom, 0, sizeof(_rom));
    memset(_have, 0, sizeof(_have));
    memset(_fail, 0, sizeof(_fail));
    for (uint8_t i = 0; i < MAX_DEVICES; i++) { _t[i] = 0; _ok_at[i] = 0; }
    _s_n = 0;
    _s_last_discrepancy = 0;
    _s_last_device = false;
    memset(_s_rom, 0, sizeof(_s_rom));
  }

  // ---- állapotgép lépései ------------------------------------------------

  void startScan() {
    _state = ST_SCANNING;
    _s_n = 0;
    _s_last_discrepancy = 0;
    _s_last_device = false;
    memset(_s_rom, 0, sizeof(_s_rom));
  }

  void scanStep(uint32_t now) {
    uint8_t addr[8];
    bool found = (_s_n < MAX_DEVICES) && searchNext(addr);

    if (found) {
      if (crc8(addr, 7) == addr[7] && familySupported(addr[0])) {
        bool dup = false;
        for (uint8_t i = 0; i < _s_n; i++) {
          if (memcmp(_s_list[i], addr, 8) == 0) { dup = true; break; }
        }
        if (!dup) memcpy(_s_list[_s_n++], addr, 8);
      }
      if (!_s_last_device && _s_n < MAX_DEVICES) return;   // következő hívásnál folytatja
    }

    commitScan(now);
    if (_n > 0) startConversion(now);
    else _state = ST_IDLE;
  }

  // Új eszközlista átvétele; a már ismert érzékelők utolsó értéke megmarad.
  void commitScan(uint32_t now) {
    float    t[MAX_DEVICES];
    uint32_t ok[MAX_DEVICES];
    bool     have[MAX_DEVICES];

    for (uint8_t i = 0; i < _s_n; i++) {
      have[i] = false; t[i] = 0; ok[i] = 0;
      for (uint8_t j = 0; j < _n; j++) {
        if (memcmp(_rom[j], _s_list[i], 8) == 0) {
          have[i] = _have[j]; t[i] = _t[j]; ok[i] = _ok_at[j];
          break;
        }
      }
    }
    for (uint8_t i = 0; i < _s_n; i++) {
      memcpy(_rom[i], _s_list[i], 8);
      _have[i] = have[i]; _t[i] = t[i]; _ok_at[i] = ok[i]; _fail[i] = 0;
    }
    for (uint8_t i = _s_n; i < MAX_DEVICES; i++) _have[i] = false;

    _n = _s_n;
    _scanned = true;
    _need_rescan = false;
    _scan_at = now;
  }

  void startConversion(uint32_t now) {
    if (!busReset()) {
      _need_rescan = true;          // nincs jelenlét-impulzus: a köv. ciklusban újrakeres
      _state = ST_IDLE;
      return;
    }
    writeByte(0xCC);                // SKIP ROM: minden érzékelő egyszerre mér
    writeByte(0x44);                // CONVERT T
    _conv_at = now;
    _state = ST_CONVERTING;
  }

  void readDevice(uint8_t idx, uint32_t now) {
    uint8_t sp[9];
    bool ok = false;

    for (uint8_t attempt = 0; attempt < 2 && !ok; attempt++) {
      if (!busReset()) continue;
      if (_n == 1) {
        writeByte(0xCC);            // egyetlen eszköz: SKIP ROM
      } else {
        writeByte(0x55);            // MATCH ROM
        for (uint8_t i = 0; i < 8; i++) writeByte(_rom[idx][i]);
      }
      writeByte(0xBE);              // READ SCRATCHPAD
      for (uint8_t i = 0; i < 9; i++) sp[i] = readByte();

      float t;
      if (decodeScratchpad(sp, _rom[idx][0], t)) {
        // +85.0 °C = nem futott le konverzió (pl. tápzavar) – csak akkor
        // fogadjuk el, ha az előző érték is a közelében volt.
        if (isPowerOnValue(sp, _rom[idx][0]) && !(_have[idx] && _t[idx] > 75.0f)) {
          continue;
        }
        _t[idx] = t;
        _have[idx] = true;
        _ok_at[idx] = now;
        _fail[idx] = 0;
        ok = true;
      }
    }

    if (!ok && ++_fail[idx] >= 3) {
      _fail[idx] = 0;
      _need_rescan = true;          // több egymás utáni hiba: lekerült / cserélték?
    }
  }

  // ---- ROM-keresés (Maxim AN187, egy lépés = egy eszköz) -----------------

  bool searchNext(uint8_t out[8]) {
    if (_s_last_device) return false;
    if (!busReset()) {
      _s_last_discrepancy = 0;
      _s_last_device = true;
      return false;
    }

    uint8_t id_bit_number = 1;
    uint8_t last_zero = 0;
    uint8_t rom_byte_number = 0;
    uint8_t rom_byte_mask = 1;

    writeByte(0xF0);                // SEARCH ROM

    while (rom_byte_number < 8) {
      uint8_t id_bit = readBit();
      uint8_t cmp_id_bit = readBit();
      if (id_bit && cmp_id_bit) break;      // nincs válaszoló eszköz

      uint8_t dir;
      if (id_bit != cmp_id_bit) {
        dir = id_bit;
      } else {
        if (id_bit_number < _s_last_discrepancy) {
          dir = (_s_rom[rom_byte_number] & rom_byte_mask) ? 1 : 0;
        } else {
          dir = (id_bit_number == _s_last_discrepancy) ? 1 : 0;
        }
        if (dir == 0) last_zero = id_bit_number;
      }

      if (dir) _s_rom[rom_byte_number] |= rom_byte_mask;
      else     _s_rom[rom_byte_number] &= (uint8_t)~rom_byte_mask;

      writeBit(dir);
      id_bit_number++;
      rom_byte_mask <<= 1;
      if (rom_byte_mask == 0) {
        rom_byte_number++;
        rom_byte_mask = 1;
      }
    }

    if (id_bit_number < 65 || _s_rom[0] == 0) {
      _s_last_discrepancy = 0;
      _s_last_device = true;
      return false;
    }

    _s_last_discrepancy = last_zero;
    if (_s_last_discrepancy == 0) _s_last_device = true;
    memcpy(out, _s_rom, 8);
    return true;
  }

  // ---- hardverfüggő réteg ------------------------------------------------
  //
  // ESP32: a láb nyitott nyelőjű (open-drain) ki/bemenet, a szint írása gyors.
  // nRF52 és egyéb: "LOW" = kimenet alacsony, "elengedés" = bemenet felhúzóval.
  //
  // Megszakítás-tiltás csak a <=15 µs-os ablakokra (írás-1, olvasás mintavétel);
  // ESP32-n a jelenlét-impulzus mintavétele is védett (~70 µs).

  inline void critEnter() {
#if defined(ESP32)
    portENTER_CRITICAL(mux());
#else
    noInterrupts();
#endif
  }

  inline void critExit() {
#if defined(ESP32)
    portEXIT_CRITICAL(mux());
#else
    interrupts();
#endif
  }

  void initLine() {
    if (!active()) return;
#if defined(ESP32)
    digitalWrite(_pin, HIGH);
    pinMode(_pin, OUTPUT_OPEN_DRAIN | PULLUP);
    digitalWrite(_pin, HIGH);
#else
    pinMode(_pin, INPUT_PULLUP);
#endif
  }

  void releaseLine() {
    if (_pin >= 0) pinMode(_pin, INPUT);
  }

  inline void lineLow() {
#if defined(ESP32)
    digitalWrite(_pin, LOW);
#else
    digitalWrite(_pin, LOW);
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
#endif
  }

  inline void lineRelease() {
#if defined(ESP32)
    digitalWrite(_pin, HIGH);
#else
    pinMode(_pin, INPUT_PULLUP);
#endif
  }

  inline uint8_t lineRead() {
    return digitalRead(_pin) ? 1 : 0;
  }

  // Reset + jelenlét-impulzus. Igaz, ha legalább egy eszköz válaszolt.
  bool busReset() {
    lineRelease();
    // a vonalnak magasnak kell lennie (hiányzó felhúzó / rövidzár felismerése)
    uint8_t retries = 125;
    while (!lineRead()) {
      if (--retries == 0) return false;
      delayMicroseconds(2);
    }

    lineLow();
    delayMicroseconds(480);

#if defined(ESP32)
    critEnter();
    lineRelease();
    delayMicroseconds(70);
    uint8_t presence = !lineRead();
    critExit();
#else
    lineRelease();
    delayMicroseconds(70);
    uint8_t presence = !lineRead();
#endif

    delayMicroseconds(410);
    return presence != 0;
  }

  void writeBit(uint8_t v) {
    if (v & 1) {
      critEnter();
      lineLow();
      delayMicroseconds(6);
      lineRelease();
      critExit();
      delayMicroseconds(64);
    } else {
      // a 60 µs-os alacsony szint megnyúlása (<120 µs) nem okoz hibát
      lineLow();
      delayMicroseconds(60);
      lineRelease();
      delayMicroseconds(10);
    }
  }

  uint8_t readBit() {
    critEnter();
    lineLow();
    delayMicroseconds(3);
    lineRelease();
    delayMicroseconds(10);
    uint8_t r = lineRead();
    critExit();
    delayMicroseconds(53);
    return r;
  }

  void writeByte(uint8_t v) {
    for (uint8_t i = 0; i < 8; i++) {
      writeBit(v & 0x01);
      v >>= 1;
    }
  }

  uint8_t readByte() {
    uint8_t r = 0;
    for (uint8_t i = 0; i < 8; i++) {
      if (readBit()) r |= (uint8_t)(1 << i);
    }
    return r;
  }
};
