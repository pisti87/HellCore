#include "SpectrumScreen.h"
#include "UITask.h"

#include <stdio.h>
#include <math.h>
#include "I18n.h"
#include "MenuUi.h"

// --------------------------------------------------
// Hardverfüggő rész: a rádió csomagszámlálója és zajszint-becslése.
// Ezeken az eszközökön a radio_driver RadioLibWrapper (SX1262 / LR1110),
// így elérhető a getPacketsRecv(), getLastRSSI(), getLastSNR(), getNoiseFloor().
// Más eszközön csak az RSSI-görbe és a zajszint-sáv látszik, csomagok nélkül.
// --------------------------------------------------

#if defined(HELTEC_LORA_V3) || defined(HELTEC_LORA_V4) || defined(HELTEC_LORA_V4_R8) || defined(HELTEC_T096) || defined(HELTEC_T114) || defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9)
  #define HC_SPECTRUM_RADIO_STATS 1
  #include "target.h"
#endif

// --------------------------------------------------
// Elrendezés (logikai 128x64, de a szélesség/magasság futásidőben jön)
// --------------------------------------------------

static const int GRAPH_X      = 2;
static const int GRAPH_Y      = 21;
static const int GRAPH_BORDER = 1;

static const int LABEL_Y = 12;

static const int DETAIL_Y0    = 13;
static const int DETAIL_ROW_H = 10;
static const int DETAIL_TXT_X = 8;

// Ennyi dB-lel a zajszint fölötti minta már jelnek (csomagnak) számít,
// nem kerül a zajszint-sávba.
static const float FLOOR_SIGNAL_MARGIN_DB = 10.0f;

static const float RSSI_DEFAULT_LO = -120.0f;
static const float RSSI_DEFAULT_HI = -40.0f;

static const uint16_t SPAN_SECONDS[SpectrumScreen::SPAN_COUNT] = { 10, 60, 300 };
static const char*    SPAN_LABELS[SpectrumScreen::SPAN_COUNT]  = { "10s", "1m", "5m" };

// Egyszínű OLED-en a zajszint-sávot ritkítva (minden 2. oszlop) rajzoljuk,
// hogy a zajszint-vonal látszódjon rajta. Színes TFT-n teli sáv.
#if defined(HC_HAS_COLOR_TFT)
  static const bool SOLID_BAND = true;
#else
  static const bool SOLID_BAND = false;
#endif

// --------------------------------------------------
// Segédfüggvények
// --------------------------------------------------

uint8_t SpectrumScreen::encodeRssi(float dbm) {
  long v = lroundf(-dbm);
  if (v < 1)   v = 1;
  if (v > 255) v = 255;
  return (uint8_t)v;
}

float SpectrumScreen::decodeRssi(uint8_t v) {
  return -(float)v;
}

int SpectrumScreen::rssiToY(float rssi, float axis_lo, float axis_hi, int graph_y, int graph_h) {
  if (rssi < axis_lo) rssi = axis_lo;
  if (rssi > axis_hi) rssi = axis_hi;

  float normalized = (rssi - axis_lo) / (axis_hi - axis_lo);

  int y = graph_y + graph_h - 1 - (int)lroundf(normalized * (graph_h - 1));
  return y;
}

static void drawDashedLine(DisplayDriver& display, int x, int y, int width) {
  const int dash_width = 3;
  const int dash_gap   = 2;

  for (int pos = 0; pos < width; pos += dash_width + dash_gap) {
    int remaining = width - pos;
    int w = remaining < dash_width ? remaining : dash_width;
    display.fillRect(x + pos, y, w, 1);
  }
}

// --------------------------------------------------
// Konstruktor / reset
// --------------------------------------------------

SpectrumScreen::SpectrumScreen(
  UITask* task,
  SpectrumRSSIReader reader,
  void* context
)
  : _task(task),
    _reader(reader),
    _context(context),
    _view(VIEW_GRAPH),
    _span(1) {             // alapértelmezés: 1 perc

  reset();
}

void SpectrumScreen::reset() {
  _current_rssi = RSSI_DEFAULT_LO;
  _has_current  = false;
  _last_sample  = 0;

  memset(_buckets, 0, sizeof(_buckets));
  _t0      = millis();
  _cur_sec = 0;

  _acc_has    = false;
  _acc_lo     = 0;
  _acc_hi     = 0;
  _acc_nf_sum = 0;
  _acc_nf_n   = 0;
  _acc_pkts   = 0;

  _pkt_write        = 0;
  _pkt_count        = 0;
  _last_recv_count  = 0;
  _recv_count_valid = false;   // az első olvasás csak bázis, nem "új csomag"
}

// --------------------------------------------------
// Rádió zajszint (hardverfüggő)
// --------------------------------------------------

float SpectrumScreen::radioNoiseFloor() const {
#if defined(HC_SPECTRUM_RADIO_STATS)
  int nf = radio_driver.getNoiseFloor();
  // 0 = még nincs kalibrálva
  if (nf < 0 && nf > -150) {
    return (float)nf;
  }
#endif
  return NAN;
}

// --------------------------------------------------
// Másodperces vödrök
// --------------------------------------------------

// A nyitott másodperc lezárása és az esetleg kimaradt másodpercek ürítése.
void SpectrumScreen::closeSecond(uint32_t new_sec) {
  Bucket b;
  memset(&b, 0, sizeof(b));

  if (_acc_has) {
    b.lo = encodeRssi(_acc_lo);
    b.hi = encodeRssi(_acc_hi);
  }

  if (_acc_nf_n > 0) {
    b.nf = encodeRssi(_acc_nf_sum / _acc_nf_n);
  }

  b.pkts = _acc_pkts;

  _buckets[_cur_sec % HISTORY_SECONDS] = b;

  // kimaradt másodpercek (pl. lassú képernyőfrissítés) -> üres vödör
  uint32_t gap = new_sec - _cur_sec - 1;
  if (gap > HISTORY_SECONDS) {
    gap = HISTORY_SECONDS;
  }

  for (uint32_t k = 1; k <= gap; k++) {
    memset(&_buckets[(_cur_sec + k) % HISTORY_SECONDS], 0, sizeof(Bucket));
  }

  _cur_sec    = new_sec;
  _acc_has    = false;
  _acc_nf_sum = 0;
  _acc_nf_n   = 0;
  _acc_pkts   = 0;
}

// Egy másodperc adata (a nyitott másodpercé is). Hamis, ha nincs adat.
bool SpectrumScreen::bucketAt(uint32_t sec, Bucket& out) const {
  memset(&out, 0, sizeof(out));

  if (sec > _cur_sec) {
    return false;
  }

  if (sec == _cur_sec) {
    if (_acc_has) {
      out.lo = encodeRssi(_acc_lo);
      out.hi = encodeRssi(_acc_hi);
    }
    if (_acc_nf_n > 0) {
      out.nf = encodeRssi(_acc_nf_sum / _acc_nf_n);
    }
    out.pkts = _acc_pkts;
  } else {
    if (_cur_sec - sec >= HISTORY_SECONDS) {
      return false;
    }
    out = _buckets[sec % HISTORY_SECONDS];
  }

  return out.lo != 0 || out.nf != 0 || out.pkts != 0;
}

// --------------------------------------------------
// Csomagok
// --------------------------------------------------

void SpectrumScreen::addPacket(uint32_t now, float rssi, float snr) {
  Packet& p = _packets[_pkt_write];
  p.at_ms = now;
  p.rssi  = rssi;
  p.snr   = snr;

  _pkt_write = (_pkt_write + 1) % PACKET_MAX;
  if (_pkt_count < PACKET_MAX) {
    _pkt_count++;
  }

  if (_acc_pkts < 255) {
    _acc_pkts++;
  }
}

const SpectrumScreen::Packet& SpectrumScreen::packetAt(uint8_t i) const {
  uint8_t index = (_pkt_write + PACKET_MAX - _pkt_count + i) % PACKET_MAX;
  return _packets[index];
}

// --------------------------------------------------
// Mintavétel (a UITask hívja, amíg ez a képernyő látszik)
// --------------------------------------------------

void SpectrumScreen::poll() {
  uint32_t now = millis();

  if ((uint32_t)(now - _last_sample) < SAMPLE_INTERVAL_MS) {
    return;
  }
  _last_sample = now;

  // új másodperc?
  uint32_t sec = (uint32_t)(now - _t0) / 1000;
  if (sec != _cur_sec) {
    closeSecond(sec);
  }

  // --- vett csomagok (a rádió számlálójának növekedése) ---
#if defined(HC_SPECTRUM_RADIO_STATS)
  uint32_t rc = radio_driver.getPacketsRecv();

  if (_recv_count_valid && rc != _last_recv_count) {
    uint32_t delta = rc - _last_recv_count;
    if (delta > 4) {
      delta = 4;   // két mintavétel között ennél több csomag nem fér az éterbe
    }

    float rssi = radio_driver.getLastRSSI();
    float snr  = radio_driver.getLastSNR();

    for (uint32_t i = 0; i < delta; i++) {
      addPacket(now, rssi, snr);
    }
  }

  _last_recv_count  = rc;
  _recv_count_valid = true;
#endif

  // --- zajszint ---
  float nf = radioNoiseFloor();
  if (!isnan(nf)) {
    _acc_nf_sum += nf;
    _acc_nf_n++;
  }

  // --- pillanatnyi RSSI ---
  if (!_reader) {
    return;
  }

  float rssi = _reader(_context);

  // érvénytelen mérés: kihagyjuk
  if (isnan(rssi) || rssi > 0.0f || rssi < -150.0f) {
    return;
  }

  _current_rssi = rssi;
  _has_current  = true;

  // A zajszint-sávba csak a jelmentes minták kerülnek. Viszonyítás:
  // a rádió zajszintje, ennek hiányában az eddigi legkisebb érték.
  float ref = !isnan(nf) ? nf : (_acc_has ? _acc_lo : rssi);

  if (rssi <= ref + FLOOR_SIGNAL_MARGIN_DB) {
    if (!_acc_has) {
      _acc_lo  = rssi;
      _acc_hi  = rssi;
      _acc_has = true;
    } else {
      if (rssi < _acc_lo) _acc_lo = rssi;
      if (rssi > _acc_hi) _acc_hi = rssi;
    }
  }
}

// --------------------------------------------------
// Időablak
// --------------------------------------------------

uint32_t SpectrumScreen::spanSeconds() const {
  return SPAN_SECONDS[_span % SPAN_COUNT];
}

const char* SpectrumScreen::spanLabel() const {
  return SPAN_LABELS[_span % SPAN_COUNT];
}

// --------------------------------------------------
// Statisztika az aktuális ablakra
// --------------------------------------------------

void SpectrumScreen::computeStats(Stats& st) const {
  memset(&st, 0, sizeof(st));

  const uint32_t span = spanSeconds();
  const uint32_t first = (_cur_sec + 1 > span) ? (_cur_sec + 1 - span) : 0;

  float nf_sum = 0;
  uint16_t nf_n = 0;
  float lo_sum = 0;
  uint16_t lo_n = 0;

  for (uint32_t s = first; s <= _cur_sec; s++) {
    Bucket b;
    if (!bucketAt(s, b)) {
      continue;
    }

    if (b.lo != 0) {
      float lo = decodeRssi(b.lo);
      float hi = decodeRssi(b.hi);

      if (!st.has_floor) {
        st.floor_lo  = lo;
        st.floor_hi  = hi;
        st.has_floor = true;
      } else {
        if (lo < st.floor_lo) st.floor_lo = lo;
        if (hi > st.floor_hi) st.floor_hi = hi;
      }

      lo_sum += lo;
      lo_n++;
    }

    if (b.nf != 0) {
      nf_sum += decodeRssi(b.nf);
      nf_n++;
    }

    st.packets += b.pkts;
  }

  // zajszint: a rádió becslése, ha van; különben a sáv aljának átlaga
  if (nf_n > 0) {
    st.noise_floor = nf_sum / nf_n;
  } else if (lo_n > 0) {
    st.noise_floor = lo_sum / lo_n;
  } else {
    st.noise_floor = NAN;
  }

  // csúcs csomag az ablakban + utolsó csomag
  uint32_t now = millis();

  for (uint8_t i = 0; i < _pkt_count; i++) {
    const Packet& p = packetAt(i);
    uint32_t age_ms = now - p.at_ms;

    if (age_ms <= span * 1000UL) {
      if (!st.has_peak || p.rssi > st.peak) {
        st.peak = p.rssi;
        st.has_peak = true;
      }
    }
  }

  if (_pkt_count > 0) {
    const Packet& last = packetAt(_pkt_count - 1);
    st.has_last   = true;
    st.last_rssi  = last.rssi;
    st.last_snr   = last.snr;
    st.last_age_s = (now - last.at_ms) / 1000;
  }
}

// --------------------------------------------------
// Rajzolás – közös rész
// --------------------------------------------------

int SpectrumScreen::render(DisplayDriver& display) {
  MenuUi::header(display, TR("Spectrum"), spanLabel(), false);

  display.setTextSize(1);

  Stats st;
  computeStats(st);

  if (_view == VIEW_DETAILS) {
    renderDetails(display, st);
  } else {
    renderGraph(display, st);
  }

  // 5 perces ablaknál elég ritkábban frissíteni
  return spanSeconds() >= 300 ? 1000 : 250;
}

// --------------------------------------------------
// GRAFIKON nézet
// --------------------------------------------------

void SpectrumScreen::renderGraph(DisplayDriver& display, const Stats& st) {
  const int width  = display.width();
  const int height = display.height();

  // ---- felső sor: NF (zajszint) | PK (csúcs csomag) | csomag/perc ----

  char nf_text[14];
  char pk_text[14];
  char rate_text[14];

  if (!isnan(st.noise_floor)) {
    snprintf(nf_text, sizeof(nf_text), "NF%d", (int)lroundf(st.noise_floor));
  } else {
    snprintf(nf_text, sizeof(nf_text), "NF --");
  }

  if (st.has_peak) {
    snprintf(pk_text, sizeof(pk_text), "PK%d", (int)lroundf(st.peak));
  } else {
    snprintf(pk_text, sizeof(pk_text), "PK --");
  }

  uint32_t span = spanSeconds();
  uint32_t elapsed = _cur_sec + 1;
  uint32_t basis = elapsed < span ? elapsed : span;
  float rate = basis > 0 ? (float)st.packets * 60.0f / (float)basis : 0.0f;
  snprintf(rate_text, sizeof(rate_text), "%.1f/m", rate);

  display.setColor(UIColor::primary_txt);

  display.setCursor(GRAPH_X, LABEL_Y);
  display.print(nf_text);

  int pk_w = (int)display.getTextWidth(pk_text);
  display.setCursor((width - pk_w) / 2, LABEL_Y);
  display.print(pk_text);

  int rate_w = (int)display.getTextWidth(rate_text);
  display.setCursor(width - GRAPH_X - rate_w, LABEL_Y);
  display.print(rate_text);

  // ---- keret ----

  const int graph_w = width - 2 * GRAPH_X;
  const int graph_h = height - GRAPH_Y - 1;

  const int ix = GRAPH_X + GRAPH_BORDER;
  const int iy = GRAPH_Y + GRAPH_BORDER;
  const int iw = graph_w - 2 * GRAPH_BORDER;
  const int ih = graph_h - 2 * GRAPH_BORDER;

  display.setColor(UIColor::secondary_txt);
  display.fillRect(GRAPH_X, GRAPH_Y, graph_w, 1);
  display.fillRect(GRAPH_X, GRAPH_Y + graph_h - 1, graph_w, 1);
  display.fillRect(GRAPH_X, GRAPH_Y, 1, graph_h);
  display.fillRect(GRAPH_X + graph_w - 1, GRAPH_Y, 1, graph_h);

  // fél-ablak jelölő (pl. -30s) az alsó kereten
  display.fillRect(GRAPH_X + graph_w / 2, GRAPH_Y + graph_h - 3, 1, 2);

  display.setColor(UIColor::window_bkg);
  display.fillRect(ix, iy, iw, ih);

  if (iw <= 0 || ih <= 4) {
    return;
  }

  if (!st.has_floor && !st.has_peak) {
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(width / 2, iy + ih / 2 - 4, "--");
    return;
  }

  // ---- függőleges skála: automatikus, 10 dB-re kerekítve, min. 30 dB ----

  float v_lo = st.has_floor ? st.floor_lo : st.peak;
  float v_hi = st.has_floor ? st.floor_hi : st.peak;

  if (st.has_peak && st.peak > v_hi) v_hi = st.peak;
  if (st.has_peak && st.peak < v_lo) v_lo = st.peak;

  float axis_lo = floorf((v_lo - 5.0f) / 10.0f) * 10.0f;
  float axis_hi = ceilf((v_hi + 5.0f) / 10.0f) * 10.0f;

  if (axis_hi - axis_lo < 30.0f) {
    axis_hi = axis_lo + 30.0f;
  }

  // ---- időtengely: az ablak eleje ms-ban, _t0-tól ----

  const int64_t now_rel  = (int64_t)(uint32_t)(millis() - _t0);
  const int64_t span_ms  = (int64_t)span * 1000;
  const int64_t start_ms = now_rel - span_ms;

  // ---- zajszint-sáv (Floor range) és zajszint-vonal (Noise floor) ----

  int prev_nf_y = -1;

  for (int x = 0; x < iw; x++) {
    int64_t t_from = start_ms + (span_ms * x) / iw;
    int64_t t_to   = start_ms + (span_ms * (x + 1)) / iw - 1;

    if (t_to < 0) {
      prev_nf_y = -1;
      continue;
    }
    if (t_from < 0) {
      t_from = 0;
    }

    uint32_t s_from = (uint32_t)(t_from / 1000);
    uint32_t s_to   = (uint32_t)(t_to / 1000);

    bool  has_band = false;
    float lo = 0;
    float hi = 0;
    float nf_sum = 0;
    int   nf_n = 0;

    for (uint32_t s = s_from; s <= s_to; s++) {
      Bucket b;
      if (!bucketAt(s, b)) {
        continue;
      }

      if (b.lo != 0) {
        float blo = decodeRssi(b.lo);
        float bhi = decodeRssi(b.hi);

        if (!has_band) {
          lo = blo;
          hi = bhi;
          has_band = true;
        } else {
          if (blo < lo) lo = blo;
          if (bhi > hi) hi = bhi;
        }
      }

      if (b.nf != 0) {
        nf_sum += decodeRssi(b.nf);
        nf_n++;
      }
    }

    // sáv
    if (has_band && (SOLID_BAND || (x & 1) == 0)) {
      int y_hi = rssiToY(hi, axis_lo, axis_hi, iy, ih);
      int y_lo = rssiToY(lo, axis_lo, axis_hi, iy, ih);

      display.setColor(UIColor::secondary_txt);
      display.fillRect(ix + x, y_hi, 1, y_lo - y_hi + 1);
    }

    // zajszint-vonal: a rádió becslése, különben a sáv alja
    float nf;
    if (nf_n > 0) {
      nf = nf_sum / nf_n;
    } else if (has_band) {
      nf = lo;
    } else {
      prev_nf_y = -1;
      continue;
    }

    int y = rssiToY(nf, axis_lo, axis_hi, iy, ih);

    display.setColor(UIColor::primary_txt);

    if (prev_nf_y < 0) {
      display.fillRect(ix + x, y, 1, 1);
    } else {
      int y0 = prev_nf_y < y ? prev_nf_y : y;
      int y1 = prev_nf_y < y ? y : prev_nf_y;
      display.fillRect(ix + x, y0, 1, y1 - y0 + 1);
    }

    prev_nf_y = y;
  }

  // ---- csúcs csomag (Peak packet): szaggatott vonal ----

  if (st.has_peak) {
    int y = rssiToY(st.peak, axis_lo, axis_hi, iy, ih);
    display.setColor(UIColor::warning_txt);
    drawDashedLine(display, ix, y, iw);
  }

  // ---- csomagok (Packets): jel a tetején + pont a vételi szinten ----

  display.setColor(UIColor::primary_txt);

  for (uint8_t i = 0; i < _pkt_count; i++) {
    const Packet& p = packetAt(i);

    int64_t rel = (int64_t)(uint32_t)(p.at_ms - _t0);
    if (rel < start_ms || rel > now_rel) {
      continue;
    }

    int x = ix + (int)(((rel - start_ms) * iw) / span_ms);
    if (x >= ix + iw) {
      x = ix + iw - 1;
    }

    // jel a grafikon tetején
    display.fillRect(x, iy, 1, 3);

    // pont a vételi szinten (3x3, a kerethez igazítva)
    int y = rssiToY(p.rssi, axis_lo, axis_hi, iy, ih);
    int dx = x - 1;
    if (dx < ix) dx = ix;
    if (dx > ix + iw - 3) dx = ix + iw - 3;
    int dy = y - 1;
    if (dy < iy) dy = iy;
    if (dy > iy + ih - 3) dy = iy + ih - 3;

    display.fillRect(dx, dy, 3, 3);
  }
}

// --------------------------------------------------
// RÉSZLETEK nézet: jelmagyarázat + számok
// --------------------------------------------------

enum LegendGlyph : uint8_t { GLYPH_NONE, GLYPH_BAND, GLYPH_LINE, GLYPH_DASH, GLYPH_DOT };

static void drawGlyph(DisplayDriver& display, LegendGlyph g, int y) {
  switch (g) {
    case GLYPH_BAND:
      display.setColor(UIColor::secondary_txt);
      if (SOLID_BAND) {
        display.fillRect(0, y + 1, 6, 5);
      } else {
        for (int x = 0; x < 6; x += 2) {
          display.fillRect(x, y + 1, 1, 5);
        }
      }
      break;

    case GLYPH_LINE:
      display.setColor(UIColor::primary_txt);
      display.fillRect(0, y + 3, 6, 1);
      break;

    case GLYPH_DASH:
      display.setColor(UIColor::warning_txt);
      display.fillRect(0, y + 3, 2, 1);
      display.fillRect(4, y + 3, 2, 1);
      break;

    case GLYPH_DOT:
      display.setColor(UIColor::primary_txt);
      display.fillRect(2, y, 1, 2);
      display.fillRect(1, y + 3, 3, 3);
      break;

    default:
      break;
  }
}

static void detailRow(DisplayDriver& display, int y, LegendGlyph g,
                      const char* label, const char* value) {
  const int width = display.width();
  const int label_x = (g == GLYPH_NONE) ? 0 : DETAIL_TXT_X;

  drawGlyph(display, g, y);

  int vw = (int)display.getTextWidth(value);

  display.setColor(UIColor::secondary_txt);
  display.drawTextEllipsized(label_x, y, width - vw - label_x - 3, label);

  display.setColor(UIColor::primary_txt);
  display.setCursor(width - vw - 1, y);
  display.print(value);
}

void SpectrumScreen::renderDetails(DisplayDriver& display, const Stats& st) {
  char v[24];
  int y = DETAIL_Y0;
  const int max_y = display.height() - 8;

  // Floor range
  if (st.has_floor) {
    snprintf(v, sizeof(v), "%d..%d",
             (int)lroundf(st.floor_lo), (int)lroundf(st.floor_hi));
  } else {
    snprintf(v, sizeof(v), "--");
  }
  detailRow(display, y, GLYPH_BAND, TR("Floor range"), v);
  y += DETAIL_ROW_H;

  // Noise floor
  if (!isnan(st.noise_floor)) {
    snprintf(v, sizeof(v), "%ddBm", (int)lroundf(st.noise_floor));
  } else {
    snprintf(v, sizeof(v), "--");
  }
  detailRow(display, y, GLYPH_LINE, TR("Noise floor"), v);
  y += DETAIL_ROW_H;

  // Peak packet
  if (st.has_peak) {
    snprintf(v, sizeof(v), "%ddBm", (int)lroundf(st.peak));
  } else {
    snprintf(v, sizeof(v), "--");
  }
  detailRow(display, y, GLYPH_DASH, TR("Peak packet"), v);
  y += DETAIL_ROW_H;

  // Packets: darab + darab/perc
  uint32_t span = spanSeconds();
  uint32_t elapsed = _cur_sec + 1;
  uint32_t basis = elapsed < span ? elapsed : span;
  float rate = basis > 0 ? (float)st.packets * 60.0f / (float)basis : 0.0f;

  snprintf(v, sizeof(v), "%u %.1f/m", (unsigned)st.packets, rate);
  detailRow(display, y, GLYPH_DOT, TR("Packets"), v);
  y += DETAIL_ROW_H;

  // Last packet: RSSI, a zajszint fölötti dB, mennyi ideje
  if (y <= max_y) {
    if (st.has_last) {
      if (!isnan(st.noise_floor)) {
        snprintf(v, sizeof(v), "%d %+d %lus",
                 (int)lroundf(st.last_rssi),
                 (int)lroundf(st.last_rssi - st.noise_floor),
                 (unsigned long)st.last_age_s);
      } else {
        snprintf(v, sizeof(v), "%d %lus",
                 (int)lroundf(st.last_rssi),
                 (unsigned long)st.last_age_s);
      }
    } else {
      snprintf(v, sizeof(v), "--");
    }
    detailRow(display, y, GLYPH_NONE, TR("Last packet"), v);
    y += DETAIL_ROW_H;
  }

  // Nagyobb kijelzőn (pl. T096 160x80): utolsó csomag SNR-je és a pillanatnyi RSSI
  if (y <= max_y) {
    if (st.has_last) {
      snprintf(v, sizeof(v), "%.1fdB", st.last_snr);
    } else {
      snprintf(v, sizeof(v), "--");
    }
    detailRow(display, y, GLYPH_NONE, "SNR", v);
    y += DETAIL_ROW_H;
  }

  if (y <= max_y) {
    if (_has_current) {
      snprintf(v, sizeof(v), "%ddBm", (int)lroundf(_current_rssi));
    } else {
      snprintf(v, sizeof(v), "--");
    }
    detailRow(display, y, GLYPH_NONE, "RSSI", v);
  }
}

// --------------------------------------------------
// Bemenetkezelés
// --------------------------------------------------

bool SpectrumScreen::handleInput(char c) {
  // vissza (változatlan: Enter / Esc / bal)
  if (c == KEY_CANCEL || c == KEY_ENTER || c == KEY_LEFT) {
    if (_task) {
      _task->gotoHomeScreen();
    }
    return true;
  }

  // nézetváltás: grafikon <-> részletek
  if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_DOWN) {
    _view = (_view + 1) % VIEW_COUNT;
    return true;
  }

  // időablak: 10s -> 1m -> 5m
  if (c == KEY_PREV || c == KEY_UP) {
    _span = (_span + 1) % SPAN_COUNT;
    return true;
  }

  return false;
}
