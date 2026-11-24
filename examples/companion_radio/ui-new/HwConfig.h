#pragma once
// Hardver konfiguráció: szenzorok, pinek, hangjelzés, rezgés + néhány UI beállítás.
// Fájlba menti (/hwcfg.bin), a flash-kopás elkerülésére késleltetve.

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <helpers/HwBoards.h>

// Legnagyobb választható láb: nRF52840: P0.00..P1.15 -> 0..47,
// ESP32-S3: GPIO0..GPIO48.
#ifndef HW_MAX_PIN
  #if defined(ESP32)
    #define HW_MAX_PIN 48
  #else
    #define HW_MAX_PIN 47
  #endif
#endif

// FIGYELEM: a sorrend része a /hwcfg.bin formátumnak (bitmaszk / pins[] index),
// új elem csak a COUNT elé kerülhet. A pins[] tömb 6 elemű, így HwPin::COUNT <= 6.
// A DS18B20 a korábban üres 6. helyet kapta: a régi konfigurációs fájl
// változatlanul betölthető (ott a DS18B20 ki van kapcsolva, lába "None").
enum class HwSensor : uint8_t { Buzzer, Vibra, Gps, Env, Dht, Ds18b20, COUNT };
enum class HwPin    : uint8_t { Buzzer, Vibra, I2cSda, I2cScl, Dht, Ds18b20, COUNT };

struct HwNote { uint16_t freq; uint16_t ms; };   // freq 0 = szünet

class HwConfig {
public:
  HwConfig();

  void begin();            // betöltés + pinek beállítása
  void loop();             // hang / rezgés / késleltetett mentés
  void save();             // azonnali mentés

  // --- fényerő beállítások ---
  uint8_t brightness() const;
  void setBrightness(uint8_t value);

  // --- szenzorok ---
  bool sensorEnabled(HwSensor s) const;
  void setSensor(HwSensor s, bool on);
  bool dhtActive() const;       // DHT11 engedélyezve ÉS van választott láb
  bool ds18b20Active() const;   // DS18B20 engedélyezve ÉS van választott láb

  // --- pinek (-1 = nincs) ---
  int8_t pin(HwPin p) const;
  void   setPin(HwPin p, int8_t value);
  int8_t nextPin(HwPin p, int8_t dir) const;      // következő szabad pin (-1 is lehet)
  bool   assignDefaultPin(HwPin p);               // üres lábhoz az eszközfüggő alapérték
  static HwSensor sensorOfPin(HwPin p);
  // LoRa TX-teljesítmény léptetése: dir<0 csökkent (a minimumnál megáll),
  // dir>0 növel (a maximumnál megáll), dir==0 (OK) növel, a maximum után
  // a minimumra ugrik (soha nem ugrik véletlenül a legnagyobb értékre).
  static int stepTxPower(int current, int8_t dir, int min_dbm, int max_dbm);
  static void pinName(int8_t pin, char* out, size_t n);

  // --- UI beállítások ---
  uint8_t  timeoutIdx() const;
  void     setTimeoutIdx(uint8_t idx);
  uint8_t  timeoutCount() const;
  uint32_t timeoutMillis(uint8_t idx) const;
  void     timeoutLabel(uint8_t idx, char* out, size_t n) const;
  bool keepUsb() const;
  void setKeepUsb(bool v);
  bool msgWake() const;
  void setMsgWake(bool v);

  // --- hangjelzés / rezgés ---
  bool buzzerActive() const;    // engedélyezve ÉS van pin
  bool vibraActive() const;
  bool buzzerMuted() const;     // van pin, de ki van kapcsolva
  void messageAlert();          // bejövő üzenet: 1 csippantás + rezgés
  void ackTick();
  void startupBeep();
  void shutdownBeep();
  void testBuzzer();
  void testVibra();
  bool busy() const;

private:
  struct Data {
    uint8_t magic;
    uint8_t version;
    uint8_t sensor_mask;
    uint8_t timeout_idx;
    uint8_t keep_usb;
    uint8_t msg_wake;
    int8_t  pins[6];
    uint8_t brightness;
  } _d;

  int8_t   _bz_pin, _vib_pin;          // ténylegesen beállított pinek
  const HwNote* _seq;
  uint8_t  _seq_len, _seq_idx;
  bool     _playing;
  uint32_t _note_end;
  bool     _vib_on;
  uint32_t _vib_end;
  bool     _dirty;
  uint32_t _dirty_since;

  void setDefaults();
  bool load();
  void markDirty();
  void applyPins();
  void releasePin(int8_t p);
  void startSeq(const HwNote* seq, uint8_t len);
  void playNote();
  void stopBeep();
  void vibrate(uint16_t ms);
  static bool isReserved(int p);
  static int8_t defaultPin(HwPin p);
  static bool pinAllowed(HwPin who, int p);
  bool usedByOther(HwPin self, int p) const;
  void sanitizePins();
};

extern HwConfig hw_config;
