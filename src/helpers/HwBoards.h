#pragma once
// ==================================================
// HellCore – eszközfüggő (hardverfüggő) jelzők egy helyen
//
// Támogatott eszközök (companion / ui-new):
//   Heltec V3 ............ HELTEC_LORA_V3
//   Heltec V4 ............ HELTEC_LORA_V4    (OLED: HELTEC_LORA_V4_OLED, TFT: HELTEC_LORA_V4_TFT)
//   Heltec V4 R8 ......... HELTEC_LORA_V4_R8 (OLED: HELTEC_V4_R8_OLED,   TFT: HELTEC_V4_R8_TFT)
//   Heltec T096 .......... HELTEC_T096
//   Heltec T114 .......... HELTEC_T114       (kijelzővel: HELTEC_T114_WITH_DISPLAY)
//   LilyGo T-Deck ........ HAS_TDECK_GT911
//   ThinkNode M9 ......... HAS_THINKNODE_M9
//
// A hardverfüggő részek a kódban egységesen így néznek ki:
//
//   #if defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9) || defined(HELTEC_LORA_V4_R8)
//     ...
//   #endif
//
// A platformio.ini-k már a fenti neveket adják meg; az alábbi alias-ok
// azért vannak, hogy a régi (-D LILYGO_TDECK, -D THINKNODE_M9,
// -D HELTEC_V4_R8) build-jelzőkkel is ugyanúgy forduljon.
// ==================================================

// ---- Eszköz-alias-ok ---------------------------------------------------

#if defined(LILYGO_TDECK) && !defined(HAS_TDECK_GT911)
  #define HAS_TDECK_GT911 1
#endif

#if defined(THINKNODE_M9) && !defined(HAS_THINKNODE_M9)
  #define HAS_THINKNODE_M9 1
#endif

#if defined(HELTEC_V4_R8) && !defined(HELTEC_LORA_V4_R8)
  #define HELTEC_LORA_V4_R8 1
#endif

// ---- Eszközcsoportok ---------------------------------------------------

// ESP32-S3 alapú eszközök
#if defined(HELTEC_LORA_V3) || defined(HELTEC_LORA_V4) || defined(HELTEC_LORA_V4_R8) || defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9)
  #define HC_BOARD_ESP32S3 1
#endif

// nRF52840 alapú eszközök
#if defined(HELTEC_T096) || defined(HELTEC_T114)
  #define HC_BOARD_NRF52 1
#endif

// Színes TFT kijelzős eszközök (a többi egyszínű OLED vagy nincs kijelző)
#if defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9) || defined(HELTEC_T096) || defined(HELTEC_T114_WITH_DISPLAY) || defined(HELTEC_LORA_V4_TFT) || defined(HELTEC_V4_R8_TFT)
  #define HC_HAS_COLOR_TFT 1
#endif

// Saját ékezetrajzolással rendelkező kijelző-driver (ST7735, T096)
#if defined(HELTEC_T096)
  #define HC_HAS_ACCENT_FONT 1
#endif

// Saját kijelölésszínt adó kijelző-driver (ST7735, T096)
#if defined(HELTEC_T096)
  #define HC_HAS_SELECTED_COLORS 1
#endif

// ---- DS18B20 (1-Wire) alapértelmezett láb ------------------------------
//
// A láb a Beállítások > Pinek menüben bármikor átállítható. Az alapérték
// csak javaslat: olyan láb, amely az adott lapon a tüskesorra ki van vezetve
// és a firmware semmi másra nem használja. Felülírható: -D ENV_DS18B20_PIN=x
//
// Bekötés: DATA -> választott láb, VDD -> 3V3, GND -> GND,
//          4.7k felhúzó ellenállás a DATA és a 3V3 közé (kötelező!).
//          Parazita (2 vezetékes) táplálás NEM támogatott.

#if defined(ENV_DS18B20_PIN)
  #define HC_DS18B20_DEFAULT_PIN (ENV_DS18B20_PIN)
#elif defined(HELTEC_LORA_V3)
  #define HC_DS18B20_DEFAULT_PIN 7          // GPIO7 (J3 tüskesor)
#elif defined(HELTEC_LORA_V4) || defined(HELTEC_LORA_V4_R8)
  #define HC_DS18B20_DEFAULT_PIN 6          // GPIO6 (a GPIO7 itt a FEM tápja!)
#elif defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9) || defined(HELTEC_T096) || defined(HELTEC_T114)
  #define HC_DS18B20_DEFAULT_PIN -1         // nincs biztonságos alapláb: a menüben kell választani
#else
  #define HC_DS18B20_DEFAULT_PIN -1
#endif

// DS18B20 egy buszon kezelt eszközeinek legnagyobb száma
#ifndef HC_DS18B20_MAX_DEVICES
  #if defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9) || defined(HELTEC_LORA_V4) || defined(HELTEC_LORA_V4_R8)
    #define HC_DS18B20_MAX_DEVICES 8        // ESP32-S3, bőven van RAM
  #else
    #define HC_DS18B20_MAX_DEVICES 4
  #endif
#endif
