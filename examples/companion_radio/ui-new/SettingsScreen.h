#pragma once
// Beállítások menü (a körhinta utolsó előtti oldaláról OK-val érhető el).
//
// Navigáció: KEY_PREV / KEY_NEXT lép a sorok között, KEY_LEFT / KEY_RIGHT értéket vált,
// KEY_ENTER (OK) kapcsol / lefuttat, KEY_CANCEL visszalép. Minden listában van egy
// "< Back" sor az első helyen – egygombos eszközön is kilépthető.
// Újraindítás / Kikapcsolás: kétszer kell OK-t nyomni 3 mp-en belül.
//
// Az Érzékelők és Pinek almenü közvetlenül a hw_config-ot használja.
// A DHT11 / DS18B20 sorok csak akkor jelennek meg, ha a backend támogatja
// (dhtSupported() / dsSupported()).
//
// Színek: a kijelölés színe a MenuUi.h segédjeiből jön, így a szines (T096)
// és az egyszínű (Heltec V3 OLED, T114) kijelzőn is olvasható.

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "HwConfig.h"

enum class SettingsItem : uint8_t {
  LANGUAGE,         // English / Magyar / Deutsch
  SCREEN_TIMEOUT,   // 5s ... 5min
  BRIGHTNESS,       // Kijelző fényereje
  KEEP_USB,         // képernyő bekapcsolva marad USB táplálásnál
  MSG_WAKE,         // új üzenet felébreszti a képernyőt
  BLUETOOTH,
  GPS,
  BUZZER,           // hangjelzés be/ki
  VIBRA,            // rezgés be/ki
  DHT11,            // DHT11 hőmérséklet/páratartalom be/ki (ha támogatott)
  DS18B20,          // DS18B20 (1-Wire) hőmérő be/ki (ha támogatott)
  TX_POWER,         // LoRa adóteljesítmény (dBm)
  FLOOD_ADVERT,     // akció: advert küldése
  SENSORS,          // almenü: mely szenzorokat használjuk
  PINS,             // almenü: a kiválasztott szenzorok pinjei
  REBOOT,
  POWER_OFF,
  COUNT
};

class SettingsBackend {
public:
  virtual ~SettingsBackend() {}
  // Jobb oldali érték szövege ("On", "30s" ...). Üres = nincs.
  virtual void getValue(SettingsItem item, char* out, size_t n) = 0;
  // dir +1 / -1 = RIGHT / LEFT, 0 = OK (kapcsol, léptet vagy végrehajt).
  virtual void change(SettingsItem item, int8_t dir) = 0;
  // BACK a főmenüben: vissza a körhintára.
  virtual void back() = 0;

  // Támogatja-e az eszköz a DHT11-et (a menüsorok elrejtéséhez).
  virtual bool dhtSupported() { return false; }
  // Támogatja-e az eszköz a DS18B20-at (a menüsorok elrejtéséhez).
  virtual bool dsSupported() { return false; }
  // A hw_config szenzor-/pin-beállítása megváltozott (a backend alkalmazza).
  virtual void onHwConfigChanged() {}
};

class SettingsScreen : public UIScreen {
public:
  explicit SettingsScreen(SettingsBackend* backend);

  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;
  void poll() override;

  void resetSelection();

private:
  enum Page : uint8_t { PAGE_MAIN, PAGE_SENSORS, PAGE_PINS };

  SettingsBackend* _backend;
  uint8_t  _page;
  uint8_t  _selected;
  uint8_t  _first_visible;
  int8_t   _confirm;            // megerősítésre váró elem, -1 = nincs
  uint32_t _confirm_until;

  SettingsItem _main_items[(uint8_t)SettingsItem::COUNT];
  uint8_t      _main_count;
  HwSensor     _vis_sensors[(uint8_t)HwSensor::COUNT];
  uint8_t      _sens_count;
  HwPin        _vis_pins[(uint8_t)HwPin::COUNT];
  uint8_t      _vis_count;

  void rebuildMain();
  void rebuildSensors();
  void rebuildPins();
  int  rowCount() const;
  void getRow(int row, char* label, size_t ln, char* value, size_t vn);
  void move(int8_t dir);
  void act(int8_t dir);
  void actMain(SettingsItem item, int8_t dir);
  void actSensor(HwSensor s);
  void actPin(HwPin p, int8_t dir);
  void goBack();
  bool hidden(HwSensor s) const;     // nem támogatott szenzor -> rejtett sor
  uint8_t mainRowOf(SettingsItem item) const;
};
