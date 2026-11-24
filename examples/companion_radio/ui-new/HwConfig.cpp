
#include "HwConfig.h"
#include <string.h>
#include <stdio.h>
#if defined(NRF52_PLATFORM) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(NRF52832_XXAA)
  #include <InternalFileSystem.h>
  using namespace Adafruit_LittleFS_Namespace;
  #define HW_FS_NRF 1
#elif defined(ESP32)
  #include <SPIFFS.h>
  #define HW_FS_ESP 1
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS 15000
#endif

#define HW_CFG_PATH            "/hwcfg.bin"
#define HW_CFG_MAGIC           0xA5
#define HW_CFG_VERSION         2
#define HW_CFG_LEGACY_VERSION  1

#define HW_DEFAULT_BRIGHTNESS  100

// Alapértelmezett hangjelző láb.
// nRF52: P0.29. ESP32-n NINCS alapértelmezett láb: az ESP32-S3-on a GPIO26..32
// a flash/PSRAM lába, egy "29"-es láb kimenetre állítása lefagyást okozna.
#ifndef HW_DEFAULT_BUZZER_PIN
  #if defined(ESP32)
    #define HW_DEFAULT_BUZZER_PIN -1
  #else
    #define HW_DEFAULT_BUZZER_PIN 29        // P0.29
  #endif
#endif

static_assert((uint8_t)HwPin::COUNT <= 6, "A pins[] tomb csak 6 elemu");
static_assert((uint8_t)HwSensor::COUNT <= 8, "A sensor_mask csak 8 bites");

// --------------------------------------------------
// Hangkeltés: nRF52 / egyéb: tone(); ESP32: LEDC
// (az arduino-esp32 2.x-ben nincs megbízható tone())
// --------------------------------------------------

#if defined(ESP32)
  #ifndef HW_LEDC_CHANNEL
    #define HW_LEDC_CHANNEL 7
  #endif

  static int g_tone_pin = -1;

  static void hwToneStart(int pin, unsigned freq) {
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    if (g_tone_pin != pin) {
      if (g_tone_pin >= 0) ledcDetach(g_tone_pin);
      ledcAttach(pin, freq, 10);
      g_tone_pin = pin;
    }
    ledcWriteTone(pin, freq);
  #else
    if (g_tone_pin != pin) {
      if (g_tone_pin >= 0) ledcDetachPin(g_tone_pin);
      ledcSetup(HW_LEDC_CHANNEL, freq, 10);
      ledcAttachPin(pin, HW_LEDC_CHANNEL);
      g_tone_pin = pin;
    }
    ledcWriteTone(HW_LEDC_CHANNEL, freq);
  #endif
  }

  static void hwToneStop(int pin) {
    if (g_tone_pin != pin) return;
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteTone(pin, 0);
    ledcDetach(pin);
  #else
    ledcWriteTone(HW_LEDC_CHANNEL, 0);
    ledcDetachPin(pin);
  #endif
    g_tone_pin = -1;
  }
#else
  static void hwToneStart(int pin, unsigned freq) { tone(pin, freq); }
  static void hwToneStop(int pin) { noTone(pin); }
#endif

HwConfig hw_config;

static const uint32_t TIMEOUTS[] = {
  5000, 10000, 15000, 30000, 60000, 120000, 300000
};

static const uint8_t N_TIMEOUTS = 7;

static const HwNote SEQ_MSG[]      = {{2400, 120}};
static const HwNote SEQ_ACK[]      = {{1800, 30}};
static const HwNote SEQ_TEST[]     = {{2000, 100}};
static const HwNote SEQ_START[]    = {{1500, 80}, {0, 30}, {2200, 100}};
static const HwNote SEQ_SHUTDOWN[] = {{2200, 80}, {0, 30}, {1500, 100}};

static inline bool timeReached(uint32_t now, uint32_t target) {
  return (int32_t)(now - target) >= 0;
}

// --------------------------------------------------
// Konstruktor
// --------------------------------------------------

HwConfig::HwConfig()
  : _bz_pin(-1),
    _vib_pin(-1),
    _seq(NULL),
    _seq_len(0),
    _seq_idx(0),
    _playing(false),
    _note_end(0),
    _vib_on(false),
    _vib_end(0),
    _dirty(false),
    _dirty_since(0) {
  setDefaults();
}

// --------------------------------------------------
// Alapértelmezett beállítások
// --------------------------------------------------

void HwConfig::setDefaults() {
  memset(&_d, 0, sizeof(_d));

  _d.magic = HW_CFG_MAGIC;
  _d.version = HW_CFG_VERSION;

  _d.sensor_mask = 1 << (uint8_t)HwSensor::Buzzer;

#if ENV_INCLUDE_GPS == 1
  _d.sensor_mask |= 1 << (uint8_t)HwSensor::Gps;
#endif

  // Mind a hat pinmező megmarad.
  for (uint8_t i = 0; i < 6; i++) {
    _d.pins[i] = -1;
  }

  // Alapértelmezett kijelzőfényerő.
  _d.brightness = HW_DEFAULT_BRIGHTNESS;

  for (uint8_t i = 0; i < (uint8_t)HwPin::COUNT; i++) {
    _d.pins[i] = defaultPin((HwPin)i);
  }

#ifdef PIN_VIBRATION
  _d.sensor_mask |= 1 << (uint8_t)HwSensor::Vibra;
#endif

#if ENV_INCLUDE_DHT11 == 1
  // fordítási időben engedélyezett DHT11: induláskor be van kapcsolva
  _d.sensor_mask |= 1 << (uint8_t)HwSensor::Dht;
#endif

#if ENV_INCLUDE_DS18B20 == 1
  // fordítási időben engedélyezett DS18B20: induláskor be van kapcsolva
  _d.sensor_mask |= 1 << (uint8_t)HwSensor::Ds18b20;
#endif

  // Képernyő-időzítés: a legközelebbi érték az AUTO_OFF_MILLIS-hez.
  if (AUTO_OFF_MILLIS == 0) {
    _d.timeout_idx = N_TIMEOUTS;   // Never
  } else {
    uint8_t best = 0;
    uint32_t best_diff = 0xFFFFFFFF;

    for (uint8_t i = 0; i < N_TIMEOUTS; i++) {
      uint32_t diff =
        TIMEOUTS[i] > AUTO_OFF_MILLIS
          ? TIMEOUTS[i] - AUTO_OFF_MILLIS
          : AUTO_OFF_MILLIS - TIMEOUTS[i];

      if (diff < best_diff) {
        best_diff = diff;
        best = i;
      }
    }

    _d.timeout_idx = best;
  }

#ifdef KEEP_DISPLAY_ON_USB
  _d.keep_usb = 1;
#endif

  _d.msg_wake = 1;
}

// --------------------------------------------------
// Alapértelmezett lábak (a board / build jelzők szerint)
// --------------------------------------------------

int8_t HwConfig::defaultPin(HwPin p) {
  switch (p) {
    case HwPin::Buzzer:
#ifdef PIN_BUZZER
      return (int8_t)PIN_BUZZER;
#else
      return (int8_t)HW_DEFAULT_BUZZER_PIN;
#endif

    case HwPin::Vibra:
#ifdef PIN_VIBRATION
      return (int8_t)PIN_VIBRATION;
#else
      return -1;
#endif

    case HwPin::I2cSda:
#ifdef PIN_BOARD_SDA
      return (int8_t)PIN_BOARD_SDA;
#else
      return -1;
#endif

    case HwPin::I2cScl:
#ifdef PIN_BOARD_SCL
      return (int8_t)PIN_BOARD_SCL;
#else
      return -1;
#endif

    case HwPin::Dht:
#if defined(ENV_DHT11_PIN) && ENV_INCLUDE_DHT11 == 1
      return (int8_t)ENV_DHT11_PIN;
#else
      return -1;
#endif

    case HwPin::Ds18b20:
      // eszközfüggő javasolt láb (HwBoards.h), -1 = a menüben kell választani
      return (int8_t)(HC_DS18B20_DEFAULT_PIN);

    default:
      return -1;
  }
}

// A saját alapértelmezett láb mindig megengedett, más csak ha nem foglalt.
bool HwConfig::pinAllowed(HwPin who, int p) {
  if (p < 0) return true;
  if (p > HW_MAX_PIN) return false;
  if (p == (int)defaultPin(who)) return true;
  return !isReserved(p);
}

// Betöltés után: a mentett konfigban lévő, a hardverre veszélyes lábak
// (pl. egy régebbi firmware által mentett flash-láb) törlése.
void HwConfig::sanitizePins() {
  for (uint8_t i = 0; i < (uint8_t)HwPin::COUNT; i++) {
    int8_t cur = _d.pins[i];

    if (pinAllowed((HwPin)i, cur)) {
      continue;
    }

    _d.pins[i] = -1;
    markDirty();
  }
}

// --------------------------------------------------
// Fájlkezelés
// --------------------------------------------------

bool HwConfig::load() {
  // Régi v1 konfiguráció: pontosan 12 bájt.
  struct LegacyData {
    uint8_t magic;
    uint8_t version;
    uint8_t sensor_mask;
    uint8_t timeout_idx;
    uint8_t keep_usb;
    uint8_t msg_wake;
    int8_t pins[6];
  };

  static_assert(sizeof(LegacyData) == 12,
                "LegacyData merete nem 12 bajt!");

  static_assert(sizeof(Data) == 13,
                "Az uj HwConfig Data merete nem 13 bajt!");

  uint8_t raw[sizeof(Data)] = {0};

  size_t file_size = 0;
  int bytes_read = -1;

#if defined(HW_FS_NRF)

  File f = InternalFS.open(HW_CFG_PATH, FILE_O_READ);

  if (!f) {
    return false;
  }

  file_size = f.size();

  if (file_size <= sizeof(raw)) {
    bytes_read = f.read(raw, file_size);
  }

  f.close();

#elif defined(HW_FS_ESP)

  File f = SPIFFS.open(HW_CFG_PATH, "r");

  if (!f) {
    return false;
  }

  file_size = f.size();

  if (file_size <= sizeof(raw)) {
    bytes_read = (int)f.read(raw, file_size);
  }

  f.close();

#else

  return false;

#endif

  // ------------------------------------------------
  // Régi v1 konfiguráció migrálása
  // ------------------------------------------------

  if (file_size == sizeof(LegacyData) &&
      bytes_read == (int)sizeof(LegacyData)) {

    LegacyData old;
    memcpy(&old, raw, sizeof(old));

    if (old.magic != HW_CFG_MAGIC ||
        old.version != HW_CFG_LEGACY_VERSION) {
      return false;
    }

    if (old.timeout_idx >= timeoutCount()) {
      return false;
    }

    // Mind a hat régi pin ellenőrzése.
    for (uint8_t i = 0; i < 6; i++) {
      if (old.pins[i] < -1 || old.pins[i] > HW_MAX_PIN) {
        return false;
      }
    }

    // Régi beállítások átvétele.
    _d.magic = HW_CFG_MAGIC;
    _d.version = HW_CFG_VERSION;
    _d.sensor_mask = old.sensor_mask;
    _d.timeout_idx = old.timeout_idx;
    _d.keep_usb = old.keep_usb;
    _d.msg_wake = old.msg_wake;

    // A hat pinérték változatlanul megmarad.
    for (uint8_t i = 0; i < 6; i++) {
      _d.pins[i] = old.pins[i];
    }

    // A régi konfigurációhoz 100%-os fényerő tartozik.
    _d.brightness = HW_DEFAULT_BRIGHTNESS;

    // A konfigurációt új formátumban kell majd menteni.
    markDirty();

    return true;
  }

  // ------------------------------------------------
  // Új v2 konfiguráció betöltése
  // ------------------------------------------------

  if (file_size != sizeof(Data) ||
      bytes_read != (int)sizeof(Data)) {
    return false;
  }

  Data tmp;
  memcpy(&tmp, raw, sizeof(tmp));

  if (tmp.magic != HW_CFG_MAGIC ||
      tmp.version != HW_CFG_VERSION) {
    return false;
  }

  if (tmp.timeout_idx >= timeoutCount()) {
    return false;
  }

  // Mind a hat pin ellenőrzése.
  for (uint8_t i = 0; i < 6; i++) {
    if (tmp.pins[i] < -1 || tmp.pins[i] > HW_MAX_PIN) {
      return false;
    }
  }

  // Csak a támogatott fényerőértékek érvényesek.
  if (tmp.brightness != 10 &&
      tmp.brightness != 20 &&
      tmp.brightness != 40 &&
      tmp.brightness != 60 &&
      tmp.brightness != 80 &&
      tmp.brightness != 100) {
    return false;
  }

  _d = tmp;

  return true;
}

// --------------------------------------------------
// Konfiguráció mentése
// --------------------------------------------------

void HwConfig::save() {
  bool saved = false;

  // Mindig az aktuális verzió kerüljön a fájlba.
  _d.magic = HW_CFG_MAGIC;
  _d.version = HW_CFG_VERSION;

#if defined(HW_FS_NRF)

  InternalFS.remove(HW_CFG_PATH);

  File f = InternalFS.open(HW_CFG_PATH, FILE_O_WRITE);

  if (f) {
    size_t written = f.write((uint8_t*)&_d, sizeof(_d));
    f.close();

    saved = (written == sizeof(_d));
  }

#elif defined(HW_FS_ESP)

  File f = SPIFFS.open(HW_CFG_PATH, "w");

  if (f) {
    size_t written = f.write((uint8_t*)&_d, sizeof(_d));
    f.close();

    saved = (written == sizeof(_d));
  }

#endif

  // Sikertelen mentésnél maradjon dirty állapotban, de legközelebb
  // csak 2 másodperc múlva próbálja újra (ne terhelje a fő ciklust).
  if (saved) {
    _dirty = false;
  } else {
    _dirty_since = millis();
  }
}

// --------------------------------------------------
// Módosításjelzés
// --------------------------------------------------

void HwConfig::markDirty() {
  _dirty = true;
  _dirty_since = millis();
}

// --------------------------------------------------
// Inicializálás
// --------------------------------------------------

void HwConfig::begin() {
  if (!load()) {
    setDefaults();
  }

  sanitizePins();
  applyPins();
}

// --------------------------------------------------
// Fő ciklus
// --------------------------------------------------

void HwConfig::loop() {
  uint32_t now = millis();

  if (_playing && timeReached(now, _note_end)) {
    _seq_idx++;

    if (_seq_idx < _seq_len) {
      playNote();
    } else {
      stopBeep();
    }
  }

  if (_vib_on && timeReached(now, _vib_end)) {
    if (_vib_pin >= 0) {
      digitalWrite(_vib_pin, LOW);
    }

    _vib_on = false;
  }

  if (_dirty && (now - _dirty_since) > 2000) {
    save();
  }
}

// --------------------------------------------------
// Szenzorok / pinek
// --------------------------------------------------

bool HwConfig::sensorEnabled(HwSensor s) const {
  return (_d.sensor_mask >> (uint8_t)s) & 1;
}

void HwConfig::setSensor(HwSensor s, bool on) {
  if (on) {
    _d.sensor_mask |= (1 << (uint8_t)s);
  } else {
    _d.sensor_mask &= ~(1 << (uint8_t)s);
  }

  markDirty();
  applyPins();
}

bool HwConfig::dhtActive() const {
  return sensorEnabled(HwSensor::Dht) &&
         _d.pins[(uint8_t)HwPin::Dht] >= 0;
}

bool HwConfig::ds18b20Active() const {
  return sensorEnabled(HwSensor::Ds18b20) &&
         _d.pins[(uint8_t)HwPin::Ds18b20] >= 0;
}

int8_t HwConfig::pin(HwPin p) const {
  return _d.pins[(uint8_t)p];
}

void HwConfig::setPin(HwPin p, int8_t value) {
  _d.pins[(uint8_t)p] = value;

  markDirty();
  applyPins();
}

HwSensor HwConfig::sensorOfPin(HwPin p) {
  switch (p) {
    case HwPin::Buzzer:
      return HwSensor::Buzzer;

    case HwPin::Vibra:
      return HwSensor::Vibra;

    case HwPin::Dht:
      return HwSensor::Dht;

    case HwPin::Ds18b20:
      return HwSensor::Ds18b20;

    default:
      return HwSensor::Env;
  }
}

// --------------------------------------------------
// Fenntartott pinek
// --------------------------------------------------

// A rádió, a gomb és a LED pinjei nem választhatók,
// mert a módosításuk a rádió működését veszélyeztetné.

bool HwConfig::isReserved(int p) {

#ifdef P_LORA_NSS
  if (p == (int)P_LORA_NSS) return true;
#endif

#ifdef P_LORA_SCLK
  if (p == (int)P_LORA_SCLK) return true;
#endif

#ifdef P_LORA_MISO
  if (p == (int)P_LORA_MISO) return true;
#endif

#ifdef P_LORA_MOSI
  if (p == (int)P_LORA_MOSI) return true;
#endif

#ifdef P_LORA_DIO_1
  if (p == (int)P_LORA_DIO_1) return true;
#endif
  // LoRa RESET – eszközfüggő (a Heltec V3-on nincs bekötve: 0xFFFFFFFF,
  // int-ként -1, így sosem egyezik egy valódi lábbal).
#if defined(HELTEC_LORA_V3) || defined(HELTEC_LORA_V4) || defined(HELTEC_LORA_V4_R8) || defined(HELTEC_T096) || defined(HELTEC_T114) || defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9)
  #ifdef P_LORA_RESET
  if ((int)P_LORA_RESET >= 0 && p == (int)P_LORA_RESET) return true;
  #endif
#endif

#ifdef P_LORA_BUSY
  if (p == (int)P_LORA_BUSY) return true;
#endif

#ifdef PIN_USER_BTN
  if (p == (int)PIN_USER_BTN) return true;
#endif

#ifdef PIN_GPS_RX
  if (p == (int)PIN_GPS_RX) return true;
#endif

#ifdef PIN_GPS_TX
  if (p == (int)PIN_GPS_TX) return true;
#endif

#ifdef PIN_GPS_EN
  if (p == (int)PIN_GPS_EN) return true;
#endif

#ifdef PIN_GPS_RESET
  if (p == (int)PIN_GPS_RESET) return true;
#endif

#ifdef PIN_TFT_SCL
  if (p == (int)PIN_TFT_SCL) return true;
#endif

#ifdef PIN_TFT_SDA
  if (p == (int)PIN_TFT_SDA) return true;
#endif

#ifdef PIN_TFT_RST
  if (p == (int)PIN_TFT_RST) return true;
#endif

#ifdef PIN_TFT_CS
  if (p == (int)PIN_TFT_CS) return true;
#endif

#ifdef PIN_TFT_DC
  if (p == (int)PIN_TFT_DC) return true;
#endif

#ifdef PIN_TFT_LEDA_CTL
  if (p == (int)PIN_TFT_LEDA_CTL) return true;
#endif

#ifdef PIN_BAT_CTL
  if (p == (int)PIN_BAT_CTL) return true;
#endif

#ifdef PIN_VEXT_EN
  if (p == (int)PIN_VEXT_EN) return true;
#endif

#ifdef PIN_STATUS_LED
  if (p == (int)PIN_STATUS_LED) return true;
#endif

#ifdef PIN_VBAT_READ
  if (p == (int)PIN_VBAT_READ) return true;
#endif

#ifdef P_LORA_TX_LED
  if (p == (int)P_LORA_TX_LED) return true;
#endif

#ifdef P_LORA_PA_POWER
  if (p == (int)P_LORA_PA_POWER) return true;
#endif

#ifdef P_LORA_KCT8103L_PA_CSD
  if (p == (int)P_LORA_KCT8103L_PA_CSD) return true;
#endif

#ifdef P_LORA_KCT8103L_PA_CTX
  if (p == (int)P_LORA_KCT8103L_PA_CTX) return true;
#endif

#ifdef SX126X_POWER_EN
  if (p == (int)SX126X_POWER_EN) return true;
#endif

#ifdef PIN_TFT_VDD_CTL
  if (p == (int)PIN_TFT_VDD_CTL) return true;
#endif

#ifdef PIN_WIRE_SDA
  if (p == (int)PIN_WIRE_SDA) return true;
#endif

#ifdef PIN_WIRE_SCL
  if (p == (int)PIN_WIRE_SCL) return true;
#endif

#ifdef PIN_WIRE1_SDA
  if (p == (int)PIN_WIRE1_SDA) return true;
#endif

#ifdef PIN_WIRE1_SCL
  if (p == (int)PIN_WIRE1_SCL) return true;
#endif

#ifdef PIN_BOARD_SDA
  if (p == (int)PIN_BOARD_SDA) return true;
#endif

#ifdef PIN_BOARD_SCL
  if (p == (int)PIN_BOARD_SCL) return true;
#endif

#ifdef PIN_3V3_EN
  if (p == (int)PIN_3V3_EN) return true;
#endif

#ifdef PIN_ADC_CTRL
  if (p == (int)PIN_ADC_CTRL) return true;
#endif

#ifdef PIN_PERF_POWERON
  if (p == (int)PIN_PERF_POWERON) return true;
#endif

#ifdef P_LORA_GC1109_PA_EN
  if (p == (int)P_LORA_GC1109_PA_EN) return true;
#endif

#ifdef P_LORA_GC1109_PA_TX_EN
  if (p == (int)P_LORA_GC1109_PA_TX_EN) return true;
#endif

#if defined(ESP32)
  // ESP32 lábak, amelyek soha nem használhatók általános célra.
  #if defined(CONFIG_IDF_TARGET_ESP32S3)
    // GPIO22..25: nem létező láb; GPIO26..32: SPI flash / PSRAM; GPIO19/20: USB D-/D+;
    // GPIO43/44: UART0 (USB-soros híd); GPIO45/46: strapping.
    if (p >= 22 && p <= 32) return true;
    if (p == 19 || p == 20) return true;
    if (p == 43 || p == 44) return true;
    if (p == 45 || p == 46) return true;
  #elif defined(CONFIG_IDF_TARGET_ESP32C3)
    if (p >= 12 && p <= 17) return true;    // SPI flash
    if (p == 18 || p == 19) return true;    // USB D-/D+
  #elif defined(CONFIG_IDF_TARGET_ESP32)
    if (p >= 6 && p <= 11) return true;     // SPI flash
  #endif

#endif

  // ---- eszközfüggő fedélzeti perifériák ----------------------------------

#if defined(HELTEC_LORA_V3) || defined(HELTEC_LORA_V4) || defined(HELTEC_LORA_V4_R8)
  // Heltec V3 / V4 / V4 R8:
  // 1 = akku ADC, 15/16 = 32 kHz kristály, 35 = LED (V3/V4), 36 = Vext (V3/V4),
  // 37 = ADC_Ctrl.
  if (p == 1 || p == 15 || p == 16) return true;
  if (p == 35 || p == 36 || p == 37) return true;
#endif

#if defined(HELTEC_LORA_V3) || defined(HELTEC_LORA_V4_OLED) || defined(HELTEC_V4_R8_OLED)
  // OLED: 17/18 = SDA/SCL, 21 = RST
  if (p == 17 || p == 18 || p == 21) return true;
#endif

#if defined(HELTEC_LORA_V4) || defined(HELTEC_LORA_V4_R8)
  // V4 / V4 R8 FEM (PA): 2 = CSD, 5 = CTX, 7 = VFEM tápja; 46 = PA mód / LED (R8)
  if (p == 2 || p == 5 || p == 7 || p == 46) return true;
#endif

#if defined(HELTEC_LORA_V4_R8)
  // ESP32-S3R8: az oktális PSRAM a GPIO33..37 lábakat foglalja
  if (p >= 33 && p <= 37) return true;
#endif

#if defined(HAS_TDECK_GT911)
  // LilyGo T-Deck: 1/2/3/15 = trackball, 4 = akku ADC, 5/6/7 = I2S hangszóró,
  // 8/18 = billentyűzet + GT911 I2C, 10 = periféria táp, 16 = GT911 INT,
  // 39 = SD CS, 46 = billentyűzet INT.
  if (p >= 1 && p <= 8) return true;
  if (p == 10 || p == 15 || p == 16 || p == 18) return true;
  if (p == 39 || p == 46) return true;
#endif

#if defined(HELTEC_T096) || defined(HELTEC_T114)
  // nRF52840: P0.00/P0.01 = 32 kHz kristály, P0.18 = nRESET,
  // P0.09/P0.10 = NFC (GPIO-ként csak külön beállítással).
  if (p == 0 || p == 1 || p == 18) return true;
  if (p == 9 || p == 10) return true;
#endif

  (void)p;
  return false;
}

// --------------------------------------------------
// Pin használat ellenőrzése
// --------------------------------------------------

bool HwConfig::usedByOther(HwPin self, int p) const {
  for (uint8_t i = 0; i < (uint8_t)HwPin::COUNT; i++) {
    if (i != (uint8_t)self && _d.pins[i] == p) {
      return true;
    }
  }

  return false;
}

// --------------------------------------------------
// Következő pin
// --------------------------------------------------

// Ha a láb még nincs kiválasztva, az eszközfüggő alapértelmezett lábat adja,
// feltéve hogy szabad. Igaz, ha ezután van láb.
bool HwConfig::assignDefaultPin(HwPin p) {
  if (_d.pins[(uint8_t)p] >= 0) return true;

  int8_t def = defaultPin(p);
  if (def < 0 || !pinAllowed(p, def) || usedByOther(p, def)) return false;

  setPin(p, def);
  return true;
}

int8_t HwConfig::nextPin(HwPin p, int8_t dir) const {
  const int range = HW_MAX_PIN + 2;  // -1 .. HW_MAX_PIN

  int idx = _d.pins[(uint8_t)p] + 1;

  for (int k = 0; k < range; k++) {
    idx = (idx + (dir < 0 ? -1 : 1) + range) % range;

    int cand = idx - 1;

    if (cand < 0) {
      return -1;
    }

    if (pinAllowed(p, cand) && !usedByOther(p, cand)) {
      return (int8_t)cand;
    }
  }

  return _d.pins[(uint8_t)p];
}

// --------------------------------------------------
// TX-teljesítmény léptetése
// --------------------------------------------------

int HwConfig::stepTxPower(int current, int8_t dir, int min_dbm, int max_dbm) {
  if (current < min_dbm) current = min_dbm;
  if (current > max_dbm) current = max_dbm;

  if (dir < 0) {
    return current > min_dbm ? current - 1 : min_dbm;
  }

  if (dir > 0) {
    return current < max_dbm ? current + 1 : max_dbm;
  }

  return current >= max_dbm ? min_dbm : current + 1;
}

// --------------------------------------------------
// Pin neve
// --------------------------------------------------

void HwConfig::pinName(int8_t pin, char* out, size_t n) {
  if (pin < 0) {
    snprintf(out, n, "None");
    return;
  }

#if defined(HW_FS_NRF)
  snprintf(out, n, "P%d.%02d", pin / 32, pin % 32);
#else
  snprintf(out, n, "GPIO%d", pin);
#endif
}

// --------------------------------------------------
// UI beállítások
// --------------------------------------------------

uint8_t HwConfig::timeoutIdx() const {
  return _d.timeout_idx;
}

void HwConfig::setTimeoutIdx(uint8_t idx) {
  if (idx >= timeoutCount()) {
    idx = 0;
  }

  if (_d.timeout_idx == idx) {
    return;
  }

  _d.timeout_idx = idx;
  markDirty();
}

uint8_t HwConfig::timeoutCount() const {
  return N_TIMEOUTS + (AUTO_OFF_MILLIS == 0 ? 1 : 0);
}

uint32_t HwConfig::timeoutMillis(uint8_t idx) const {
  return idx < N_TIMEOUTS ? TIMEOUTS[idx] : 0;
}

void HwConfig::timeoutLabel(uint8_t idx, char* out, size_t n) const {
  uint32_t ms = timeoutMillis(idx);

  if (ms == 0) {
    snprintf(out, n, "Never");
  } else if (ms < 60000) {
    snprintf(out, n, "%ds", (int)(ms / 1000));
  } else {
    snprintf(out, n, "%dmin", (int)(ms / 60000));
  }
}

// --------------------------------------------------
// USB / üzenet-ébresztés
// --------------------------------------------------

bool HwConfig::keepUsb() const {
  return _d.keep_usb;
}

void HwConfig::setKeepUsb(bool v) {
  uint8_t value = v ? 1 : 0;

  if (_d.keep_usb == value) {
    return;
  }

  _d.keep_usb = value;
  markDirty();
}

bool HwConfig::msgWake() const {
  return _d.msg_wake;
}

void HwConfig::setMsgWake(bool v) {
  uint8_t value = v ? 1 : 0;

  if (_d.msg_wake == value) {
    return;
  }

  _d.msg_wake = value;
  markDirty();
}

// --------------------------------------------------
// Kijelzőfényerő
// --------------------------------------------------

uint8_t HwConfig::brightness() const {
  return _d.brightness;
}

void HwConfig::setBrightness(uint8_t value) {
  // A támogatott fényerőértékek:
  // 10%, 20%, 40%, 60%, 80%, 100%.

  if (value != 10 &&
      value != 20 &&
      value != 40 &&
      value != 60 &&
      value != 80 &&
      value != 100) {
    return;
  }

  if (_d.brightness == value) {
    return;
  }

  _d.brightness = value;
  markDirty();
}

// --------------------------------------------------
// Hangjelzés / rezgés
// --------------------------------------------------

bool HwConfig::buzzerActive() const {
  return sensorEnabled(HwSensor::Buzzer) &&
         _d.pins[(uint8_t)HwPin::Buzzer] >= 0;
}

bool HwConfig::vibraActive() const {
  return sensorEnabled(HwSensor::Vibra) &&
         _d.pins[(uint8_t)HwPin::Vibra] >= 0;
}

bool HwConfig::buzzerMuted() const {
  return !sensorEnabled(HwSensor::Buzzer) &&
         _d.pins[(uint8_t)HwPin::Buzzer] >= 0;
}

// --------------------------------------------------
// Pin felszabadítása
// --------------------------------------------------

void HwConfig::releasePin(int8_t p) {
  if (p < 0) {
    return;
  }

  digitalWrite(p, LOW);
  pinMode(p, INPUT);
}

// --------------------------------------------------
// Pinek alkalmazása
// --------------------------------------------------

void HwConfig::applyPins() {
  int8_t want_bz =
    buzzerActive()
      ? _d.pins[(uint8_t)HwPin::Buzzer]
      : -1;

  int8_t want_vib =
    vibraActive()
      ? _d.pins[(uint8_t)HwPin::Vibra]
      : -1;

  if (want_bz != _bz_pin) {
    stopBeep();
    releasePin(_bz_pin);

    _bz_pin = want_bz;

    if (_bz_pin >= 0) {
      pinMode(_bz_pin, OUTPUT);
      digitalWrite(_bz_pin, LOW);
    }
  }

  if (want_vib != _vib_pin) {
    if (_vib_pin >= 0) {
      digitalWrite(_vib_pin, LOW);
    }

    _vib_on = false;

    releasePin(_vib_pin);

    _vib_pin = want_vib;

    if (_vib_pin >= 0) {
      pinMode(_vib_pin, OUTPUT);
      digitalWrite(_vib_pin, LOW);
    }
  }
}

// --------------------------------------------------
// Hangjelzés indítása
// --------------------------------------------------

void HwConfig::startSeq(const HwNote* seq, uint8_t len) {
  if (_bz_pin < 0) {
    return;
  }

  _seq = seq;
  _seq_len = len;
  _seq_idx = 0;

  playNote();
}

// --------------------------------------------------
// Hang lejátszása
// --------------------------------------------------

void HwConfig::playNote() {
  const HwNote& n = _seq[_seq_idx];

  if (n.freq) {
    hwToneStart(_bz_pin, n.freq);
  } else {
    hwToneStop(_bz_pin);
  }

  _note_end = millis() + n.ms;
  _playing = true;
}

// --------------------------------------------------
// Hangjelzés leállítása
// --------------------------------------------------

void HwConfig::stopBeep() {
  if (_bz_pin >= 0) {
    hwToneStop(_bz_pin);
    digitalWrite(_bz_pin, LOW);
  }

  _playing = false;
}

// --------------------------------------------------
// Rezgés
// --------------------------------------------------

void HwConfig::vibrate(uint16_t ms) {
  if (_vib_pin < 0) {
    return;
  }

  digitalWrite(_vib_pin, HIGH);

  _vib_on = true;
  _vib_end = millis() + ms;
}

// --------------------------------------------------
// Értesítések
// --------------------------------------------------

void HwConfig::messageAlert() {
  startSeq(SEQ_MSG, 1);
  vibrate(200);
}

void HwConfig::ackTick() {
  startSeq(SEQ_ACK, 1);
}

void HwConfig::startupBeep() {
  startSeq(SEQ_START, 3);
}

void HwConfig::shutdownBeep() {
  startSeq(SEQ_SHUTDOWN, 3);
}

void HwConfig::testBuzzer() {
  startSeq(SEQ_TEST, 1);
}

void HwConfig::testVibra() {
  vibrate(150);
}

// --------------------------------------------------
// Állapot
// --------------------------------------------------

bool HwConfig::busy() const {
  return _playing || _vib_on;
}
