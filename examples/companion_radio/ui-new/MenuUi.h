
#pragma once

#include <helpers/ui/DisplayDriver.h>

// Közös menü-segédek.
//
// Kijelzőtípusok:
//   - SZINES (csak a Heltec T096, ST7735): a title_bkg eltér az ablak hátterétől.
//   - EGYSZÍNŰ (Heltec V3 OLED, Heltec T114, e-ink, ...): a title_bkg == window_bkg;
//     itt a kijelölt sor fordított (inverz) megjelenítést kap, mert a "kék" és
//     a "fehér" ugyanaz a szín, így a szöveg különben eltűnne.

namespace MenuUi {

static const uint8_t VISIBLE = 4;
static const int FIRST_Y = 13;
static const int ROW_H = 12;

// Igaz, ha a kijelző valódi színeket használ (T096).
inline bool isColorTheme() {
    return UIColor::title_bkg != UIColor::window_bkg;
}

// Kijelölt sor háttere / szövege.
// A T096 az ST7735 driver saját kijelölés-színeit használja (selected_bkg/txt),
// minden más kijelzőn a meglévő UIColor értékekből számoljuk, így nem kell
// minden driverben külön definiálni őket.
inline ColorVal selectedBkg() {
#if defined(HELTEC_T096)
    return UIColor::selected_bkg;
#else
    return isColorTheme() ? UIColor::corp_blue : UIColor::primary_txt;
#endif
}

inline ColorVal selectedTxt() {
#if defined(HELTEC_T096)
    return UIColor::selected_txt;
#else
    return isColorTheme() ? UIColor::title_txt : UIColor::window_bkg;
#endif
}

inline void header(
    DisplayDriver& display,
    const char* title,
    const char* right,
    bool
) {
    display.setColor(UIColor::title_bkg);
    display.fillRect(0, 0, display.width(), 11);

    display.setTextSize(1);
    display.setColor(UIColor::title_txt);

    display.setCursor(2, 1);
    display.print(title);

    if (right && right[0]) {
        display.drawTextRightAlign(display.width() - 2, 1, right);
    }
}

inline void row(
    DisplayDriver& display,
    int y,
    const char* label,
    const char* value,
    bool selected,
    bool warn
) {
    const int width = display.width();

    if (selected) {
        display.setColor(selectedBkg());
        display.fillRect(0, y - 1, width, ROW_H);
    }

    display.setTextSize(1);

    if (selected) {
        // Kijelölt soron mindig a kijelölés szövegszíne (egyszínű kijelzőn
        // a figyelmeztető szín megegyezne a háttérrel).
        display.setColor(selectedTxt());
    } else if (warn) {
        display.setColor(UIColor::warning_txt);
    } else {
        display.setColor(UIColor::primary_txt);
    }

    const int value_width =
        (value && value[0]) ? display.getTextWidth(value) : 0;

    const int value_x = width - value_width - 3;
    const int label_width = value_width > 0
        ? value_x - 5
        : width - 5;

    display.drawTextEllipsized(3, y, label_width, label);

    if (value && value[0]) {
        display.setCursor(value_x, y);
        display.print(value);
    }
}

}
