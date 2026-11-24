#pragma once

// Spektrum / csatornafigyelő képernyő.
//
// NEM frekvenciasöprés: a rádió a saját csatornáján marad, a mesh tovább fut.
// Időben mutatja a csatorna állapotát (10 mp / 1 perc / 5 perc ablak):
//
//   - Floor range  (zajszint-sáv):  a jelmentes RSSI-minták min..max tartománya
//   - Noise floor  (zajszint):      a rádió kalibrált zajszintje (ennek hiányában a sáv alja)
//   - Peak packet  (csúcs csomag):  az ablakban vett legerősebb csomag RSSI-je (szaggatott vonal)
//   - Packets      (csomagok):      minden vett csomag egy jel a tetején + pont a vételi szintjén
//
// Két nézet: GRAFIKON és RÉSZLETEK (jelmagyarázat + számok).
//
// Gombok:
//   KEY_NEXT / KEY_RIGHT / KEY_DOWN : nézetváltás (grafikon <-> részletek)
//   KEY_PREV / KEY_UP               : időablak (10s -> 1m -> 5m)
//   KEY_ENTER / KEY_CANCEL / KEY_LEFT : vissza a főmenübe
//   Egygombos eszközök (Heltec V3, V4, T096, T114): katt = nézet, dupla katt = időablak, hosszú = vissza.
//
// Az adatok csak akkor gyűlnek, amíg ez a képernyő látszik (a UITask csak az
// aktuális képernyő poll()-ját hívja), belépéskor a reset() törli őket.

#include <Arduino.h>
#include <stdint.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/HwBoards.h>

// A pillanatnyi RSSI dBm-ben (UITask: radio_driver.getCurrentRSSI()).
typedef float (*SpectrumRSSIReader)(void* context);

class UITask;

class SpectrumScreen : public UIScreen {
public:
  static const uint16_t SAMPLE_INTERVAL_MS = 100;   // RSSI mintavétel
  static const uint16_t HISTORY_SECONDS    = 300;   // 5 perc előzmény, másodperces vödrökben
  static const uint8_t  PACKET_MAX         = 128;   // utolsó csomagok (idő + RSSI + SNR)
  static const uint8_t  SPAN_COUNT         = 3;     // 10s, 1m, 5m

  explicit SpectrumScreen(UITask* task,
                          SpectrumRSSIReader reader,
                          void* context = nullptr);

  int  render(DisplayDriver& display) override;
  bool handleInput(char c) override;
  void poll() override;

  void reset();

private:
  enum View : uint8_t { VIEW_GRAPH, VIEW_DETAILS, VIEW_COUNT };

  // Egy másodperc összesítése. Az RSSI -dBm-ként tárolva (1..255), 0 = nincs adat.
  struct Bucket {
    uint8_t lo;      // zajszint-sáv alja
    uint8_t hi;      // zajszint-sáv teteje
    uint8_t nf;      // zajszint (rádió becslése)
    uint8_t pkts;    // ebben a másodpercben vett csomagok száma
  };

  struct Packet {
    uint32_t at_ms;  // millis() a vételkor
    float    rssi;
    float    snr;
  };

  // Egy időablak összesített adatai (render-enként számolva).
  struct Stats {
    bool     has_floor;
    float    floor_lo;
    float    floor_hi;
    float    noise_floor;
    bool     has_peak;
    float    peak;
    uint16_t packets;
    bool     has_last;
    float    last_rssi;
    float    last_snr;
    uint32_t last_age_s;
  };

  UITask*            _task;
  SpectrumRSSIReader _reader;
  void*              _context;

  uint8_t  _view;
  uint8_t  _span;

  // pillanatnyi érték
  float    _current_rssi;
  bool     _has_current;
  uint32_t _last_sample;

  // másodperces előzmény (körpuffer)
  Bucket   _buckets[HISTORY_SECONDS];
  uint32_t _t0;          // reset() ideje (millis)
  uint32_t _cur_sec;     // a nyitott (aktuális) másodperc sorszáma _t0-tól

  // a nyitott másodperc gyűjtője
  bool     _acc_has;
  float    _acc_lo;
  float    _acc_hi;
  float    _acc_nf_sum;
  uint16_t _acc_nf_n;
  uint8_t  _acc_pkts;

  // csomagok (körpuffer)
  Packet   _packets[PACKET_MAX];
  uint8_t  _pkt_write;
  uint8_t  _pkt_count;
  uint32_t _last_recv_count;
  bool     _recv_count_valid;

  // --- adatgyűjtés ---
  void  closeSecond(uint32_t new_sec);
  void  addPacket(uint32_t now, float rssi, float snr);
  float radioNoiseFloor() const;            // NAN, ha nem elérhető
  bool  bucketAt(uint32_t sec, Bucket& out) const;
  const Packet& packetAt(uint8_t i) const;  // 0 = legrégebbi

  // --- kiértékelés ---
  uint32_t spanSeconds() const;
  const char* spanLabel() const;
  void computeStats(Stats& st) const;

  // --- rajzolás ---
  void renderGraph(DisplayDriver& display, const Stats& st);
  void renderDetails(DisplayDriver& display, const Stats& st);

  static uint8_t encodeRssi(float dbm);
  static float   decodeRssi(uint8_t v);
  static int     rssiToY(float rssi, float axis_lo, float axis_hi, int graph_y, int graph_h);
};
