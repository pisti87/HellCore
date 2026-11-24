# HellCore

**[English](#english) · [Magyar](#magyar)**

> HellCore is a fork of [MeshCore](https://github.com/meshcore-dev/MeshCore) (MIT licence). The original MeshCore README
> is kept unchanged in [`docs/MESHCORE_README.md`](docs/MESHCORE_README.md); the MeshCore protocol and CLI documentation
> stays valid and lives in [`docs/`](docs/).
>
> A HellCore a [MeshCore](https://github.com/meshcore-dev/MeshCore) (MIT licenc) egy forkja. Az eredeti MeshCore README
> változatlanul a [`docs/MESHCORE_README.md`](docs/MESHCORE_README.md) fájlban van; a MeshCore protokoll- és
> CLI-dokumentáció továbbra is érvényes, a [`docs/`](docs/) mappában található.

---

<a id="english"></a>

# HellCore — English

HellCore is a custom firmware for **Heltec** LoRa devices, built on the MeshCore core. It is not a rewrite: the MeshCore
routing, radio and protocol code is unchanged. HellCore adds its own layer on top: **sensor handling and telemetry**, a
**hardware configuration UI** (sensors, pins, buzzer/vibration), an **EN / HU / DE** interface and **pin protection**.

> **Status.** Active development. The code has so far only been built and tested partially; expect small fixes after the
> first full CI run. Pin tables below come from the variant files in this repository and Heltec's datasheets —
> **always check the physical pin on your board before wiring.**

## 1. Supported devices

| Device | MCU | Display | Companion | Repeater | Sensor firmware |
|---|---|---|---|---|---|
| Heltec **V3** | ESP32-S3 | OLED | USB · BLE · WiFi | ✔ | ✔ |
| Heltec **V4** | ESP32-S3 | OLED / TFT | USB · BLE · WiFi | ✔ | ✔ |
| Heltec **T114** | nRF52840 | TFT / none | USB · BLE | ✔ | – (planned) |
| Heltec **T096** | nRF52840 | TFT | USB · BLE | ✔ | ✔ |

SenseCAP T1000-e is planned, but not part of this repository state. Other MeshCore targets under `variants/` are the
unchanged upstream ones.

PlatformIO environments (selection; the full list is in `variants/<device>/platformio.ini`):

| Device | Companion | Repeater | Sensor |
|---|---|---|---|
| V3 | `Heltec_v3_companion_radio_ble` / `_usb` / `_wifi` | `Heltec_v3_repeater` | `Heltec_v3_sensor` |
| V4 (OLED) | `heltec_v4_companion_radio_ble` / `_usb` / `_wifi` | `heltec_v4_repeater`, `heltec_v4_expansionkit_repeater` | `heltec_v4_sensor` |
| V4 (TFT) | `heltec_v4_tft_companion_radio_ble` / `_usb` / `_wifi` | `heltec_v4_tft_repeater` | `heltec_v4_tft_sensor` |
| T114 | `Heltec_t114_companion_radio_ble` / `_usb` (+ `_without_display_`) | `Heltec_t114_repeater`, `Heltec_t114_without_display_repeater` | – |
| T096 | `Heltec_t096_companion_radio_ble` / `_usb` | `Heltec_t096_repeater` | `Heltec_t096_sensor` |

## 2. What HellCore adds

### Sensors and telemetry
- **I²C sensors with automatic detection** (the bus is scanned, only devices that answer are initialised):
  BME280, BME680, BMP280, BMP085, AHT10/20/21, SHTC3, SHT4x, LPS22HB, INA219/226/260/3221, MLX90614, VL53L0X and others.
- **DHT11** — library-free driver, selectable digital pin.
- **DS18B20 (1-Wire), several devices on one pin** — library-free driver (no OneWire/DallasTemperature): ROM search,
  CRC check, non-blocking conversion, periodic re-scan so a newly connected sensor is picked up. Families `0x28`
  (DS18B20), `0x22` (DS1822), `0x10` (DS18S20), `0x3B` (MAX31850). Up to **8 devices** on the ESP32-S3 V4 and **4** on the
  other boards (`HC_DS18B20_MAX_DEVICES`, can be overridden).
- **Telemetry:** all sensors are added to the standard MeshCore (LPP / Cayenne) telemetry; every DS18B20 device is a
  separate temperature channel.
- **GPS** where the board has one.

### Companion UI
- **Settings → Sensors:** switch Buzzer, Vibration, GPS, I²C sensors, DHT11 and DS18B20 on/off at runtime.
- **Settings → Pins:** choose the pin for buzzer, vibration, DHT11 and DS18B20. Only pins that are not used by the board
  are offered; a pin can be assigned to one function only. The pin is shown in the device's own notation
  (`GPIO4` on ESP32-S3, `P0.04` on nRF52840). Stored settings are checked at start-up and an unsafe pin is cleared.
- **Sounds and vibration:** start-up, shutdown, incoming message and ACK signals.
- **Display:** brightness, screen timeout, keep-USB and wake-on-message options.
- **Languages:** English, Magyar, Deutsch (stored in `/ui.conf`).
- **Sensors page** in the main menu, listing the readings (DHT11 and every DS18B20 by name, the rest from the telemetry
  buffer). It is a compile-time option: add `-D UI_SENSORS_PAGE=1` to the companion environment to enable it (not yet set for
  the Heltec environments in this state).
- Settings are saved to `/hwcfg.bin` (delayed write to spare the flash).

### Repeater and sensor firmware
- The repeater runs the full MeshCore repeater. **`Heltec_v3_repeater` already has a DHT11 on a fixed pin (GPIO4).**
- DS18B20 and DHT11 are compiled in with `-D ENV_INCLUDE_DS18B20=1 -D ENV_DS18B20_PIN=<n>` and
  `-D ENV_INCLUDE_DHT11=1 -D ENV_DHT11_PIN=<n>`; the sensor is then enabled at boot on that pin.
- The generic MeshCore CLI (`sensor get|set|list`, see [`docs/cli_commands.md`](docs/cli_commands.md)) is available.

## 3. Pin numbering

- **ESP32-S3 (V3, V4):** GPIO numbers, e.g. `GPIO4`.
- **nRF52840 (T114, T096):** `Pn.mm` = port *n*, pin *mm*. The firmware number is `n*32 + mm` (`P0.04` = 4, `P1.10` = 42).
- The same number is a **different physical pin** on the two families: `GPIO4` ≠ `P0.04`.

## 4. Safe pins — what the firmware avoids

A pin is never offered or used if the board needs it. This covers, depending on the board: LoRa radio (SPI, NSS, DIO1, BUSY,
RESET, TX LED, PA / FEM control, radio power), display (OLED / TFT bus, reset, backlight, power), GPS (RX/TX, enable, reset),
user button, battery sense / control, Vext and other power switches, the board I²C bus, USB, SPI flash/PSRAM, UART0,
strapping pins and (nRF) the 32 kHz crystal, reset and NFC pins.

Free header pins computed from this repository's variant files (the pins the companion *Pins* menu can offer and that are
candidates for fixed repeater pins):

| Device | Free pins |
|---|---|
| Heltec V3 | GPIO2, 4, 5, 6, 7, 33, 34, 38, 39, 40, 41, 42 |
| Heltec V4 (OLED) | GPIO4, 6, 33, 40, 41, 47, 48 (TFT variant and expansion kit: GPIO3/4 are I²C, so 6, 40, 41, 47, 48) |
| Heltec T114 | P0.28, P0.29, P0.30, P0.31, P1.01 (pin map only partly verified) |
| Heltec T096 | P0.02, P0.24, P0.29, P0.31, P1.00–P1.05, P1.07, P1.13 |

Typical defaults: DS18B20 `GPIO7` (V3) and `GPIO6` (V4, where GPIO7 powers the FEM); T114 and T096 have no preset
and the pin is chosen in the menu.

## 5. Wiring

- **DS18B20** (1–8 devices on the same pin): DATA → chosen pin, VCC → 3V3, GND → GND, and a **4.7 kΩ pull-up between DATA and
  3V3 (required)**. Parasitic power is not supported.
- **DHT11:** DATA → chosen pin, VCC → 3V3, GND → GND; a 4.7–10 kΩ pull-up is recommended (the internal one is enabled too).
- **I²C sensors:** on the board's I²C bus (3V3 devices). Two sensors with the same address cannot share a bus.

⚡ **Use an external power source** (common GND with the board) for anything that draws more than a few mA: buzzers above
~10 mA, vibration motors (50–100 mA, with a transistor and flyback diode), GPS modules, relays, SD cards, LED strips, many
DS18B20 on a long cable. Never power such loads from a GPIO pin. Small sensors (DS18B20, DHT11, BME280, AHT, SHT, INA2xx)
need only a few mA and can use the board's 3V3. The Heltec V4 datasheet gives 500 mA each for 3.3 V, Vext and 5 V (USB); for the
T114 and T096 no output limit is published, so treat the on-board 3V3 conservatively. On battery or solar repeaters every
extra mA shortens the runtime.

## 6. Build

```bash
pip install platformio
./build.sh build-firmware Heltec_t096_companion_radio_ble     # result in out/ (.bin / .uf2 / .zip)
# or simply:  pio run -e Heltec_v3_repeater
```

`.github/workflows/heltec-release.yml` builds the companion targets and publishes a GitHub release on a
`heltec-v*` tag (or manually from the Actions tab). Unit tests: `.github/workflows/run-unit-tests.yml`.

## 7. Roadmap — next version

**Sensors on the repeaters** (V3, V4, T114, T096) with strictly safe pins:

- DS18B20 and DHT11 enabled in every repeater environment (not only V3 + DHT11), on **fixed pins chosen at build time**.
- One central per-device pin table. A **whitelist** of header pins is used (a pin must be brought out to the header *and*
  not used by the board), instead of only a list of known-bad pins. This also closes known gaps of the current blacklist:
  T114 battery sense and control (P0.04, P0.06) and T096 battery sense (P0.03) / user button (P1.10) are defined outside the
  macros the check reads.
- A **build-time check** (`static_assert`): a repeater with a fixed pin that collides with an on-board peripheral does not compile.
- CLI to read the sensors on the repeater (list, rename, report interval) and optional reporting as telemetry or as a message
  to the admin.
- Stable telemetry channels per DS18B20 device (currently channels are assigned in detection order).
- Proposed fixed repeater pins (to be confirmed on the hardware):

  | Device | DS18B20 | DHT11 |
  |---|---|---|
  | V3 | GPIO7 | GPIO4 |
  | V4 | GPIO6 | GPIO47 |
  | T114 | P0.29 | P0.30 |
  | T096 | P0.29 | P0.31 |

## 8. Licence

MIT, see [`LICENSE`](LICENSE). HellCore is based on [MeshCore](https://github.com/meshcore-dev/MeshCore) and its contributors (MIT licence).

---

<a id="magyar"></a>

# HellCore — Magyar

A HellCore **Heltec** LoRa eszközökre készült saját firmware, a MeshCore magjára építve. Nem újraírás: a MeshCore útválasztó,
rádiós és protokoll kódja változatlan. A HellCore a saját rétegét adja hozzá: **szenzorkezelés és telemetria**,
**hardverbeállító felület** (szenzorok, lábak, buzzer/rezgés), **EN / HU / DE** nyelvű megjelenítés és **lábvédelem**.

> **Állapot.** Aktív fejlesztés. A kód eddig csak részben lett lefordítva és letesztelve; az első teljes CI-futás után apróbb
> javítások várhatók. Az alábbi lábtáblák a repó variánsfájljaiból és a Heltec adatlapjaiból készültek —
> **bekötés előtt mindig ellenőrizd a fizikai lábat a kártyán.**

## 1. Támogatott eszközök

| Eszköz | MCU | Kijelző | Companion | Repeater | Szenzor firmware |
|---|---|---|---|---|---|
| Heltec **V3** | ESP32-S3 | OLED | USB · BLE · WiFi | ✔ | ✔ |
| Heltec **V4** | ESP32-S3 | OLED / TFT | USB · BLE · WiFi | ✔ | ✔ |
| Heltec **T114** | nRF52840 | TFT / nincs | USB · BLE | ✔ | – (tervezett) |
| Heltec **T096** | nRF52840 | TFT | USB · BLE | ✔ | ✔ |

A SenseCAP T1000-e tervezett, de ebben a repó-állapotban nincs benne. A `variants/` többi célja a változatlan MeshCore.
A PlatformIO env-ek neve az angol rész 1. táblázatában van; a teljes lista a `variants/<eszköz>/platformio.ini` fájlokban.

## 2. Mit ad hozzá a HellCore

### Szenzorok és telemetria
- **I²C szenzorok automatikus felismeréssel** (a buszt szkenneli, csak a válaszoló eszközöket inicializálja): BME280, BME680,
  BMP280, BMP085, AHT10/20/21, SHTC3, SHT4x, LPS22HB, INA219/226/260/3221, MLX90614, VL53L0X és más.
- **DHT11** — könyvtár nélküli driver, választható digitális láb.
- **DS18B20 (1-Wire), több eszköz egy lábon** — könyvtár nélküli driver (nem kell OneWire/DallasTemperature): ROM-keresés,
  CRC-ellenőrzés, nem blokkoló konverzió, időszakos újrakeresés, így az újonnan csatlakoztatott érzékelőt is felveszi.
  Támogatott családok: `0x28` (DS18B20), `0x22` (DS1822), `0x10` (DS18S20), `0x3B` (MAX31850). Legfeljebb **8 eszköz** a V4-en
  és **4** a többi eszközön (`HC_DS18B20_MAX_DEVICES`, felülírható).
- **Telemetria:** minden szenzor bekerül a szabványos MeshCore (LPP / Cayenne) telemetriába; minden DS18B20 külön
  hőmérséklet-csatornát kap.
- **GPS**, ahol a kártyán van.

### Companion felület
- **Beállítások → Szenzorok:** buzzer, rezgés, GPS, I²C szenzorok, DHT11 és DS18B20 futásidejű ki/bekapcsolása.
- **Beállítások → Pinek:** a buzzer, rezgés, DHT11 és DS18B20 lábának kiválasztása. Csak olyan lábat kínál fel, amelyet a kártya
  nem használ; egy láb csak egy funkcióhoz rendelhető. A láb az eszköz saját jelölésével jelenik meg (`GPIO4` ESP32-S3-on,
  `P0.04` nRF52840-en). A mentett beállításokat induláskor ellenőrzi, a veszélyes lábat törli.
- **Hang és rezgés:** indítási, leállítási, bejövő üzenet és ACK jelzés.
- **Kijelző:** fényerő, képernyő-időzítés, USB-n maradás és ébresztés üzenetre.
- **Nyelvek:** English, Magyar, Deutsch (a `/ui.conf` fájlba mentve).
- **Sensors oldal** a főmenüben, az értékekkel (a DHT11 és minden DS18B20 névvel, a többi a telemetria-pufferből). Fordítási
  opció: a companion env-hez add hozzá a `-D UI_SENSORS_PAGE=1` jelzőt (a Heltec env-ekben ebben az állapotban még nincs beállítva).
- A beállítások a `/hwcfg.bin` fájlba mentődnek (késleltetett írás a flash kímélésére).

### Repeater és szenzor firmware
- A repeater a teljes MeshCore repeater. **A `Heltec_v3_repeater` már tartalmaz egy DHT11-et fix lábon (GPIO4).**
- A DS18B20 és a DHT11 a `-D ENV_INCLUDE_DS18B20=1 -D ENV_DS18B20_PIN=<n>` és `-D ENV_INCLUDE_DHT11=1 -D ENV_DHT11_PIN=<n>`
  jelzőkkel fordítható be; a szenzor ekkor induláskor az adott lábon bekapcsol.
- Az általános MeshCore CLI (`sensor get|set|list`, lásd [`docs/cli_commands.md`](docs/cli_commands.md)) elérhető.

## 3. Lábszámozás

- **ESP32-S3 (V3, V4):** GPIO számok, pl. `GPIO4`.
- **nRF52840 (T114, T096):** `Pn.mm` = *n*. port, *mm*. láb. A firmware-ben a szám `n*32 + mm` (`P0.04` = 4, `P1.10` = 42).
- Ugyanaz a szám a két családon **más fizikai láb**: `GPIO4` ≠ `P0.04`.

## 4. Biztonságos lábak — mit kerül el a firmware

Egy lábat soha nem kínál fel és nem használ, ha a kártyának szüksége van rá. Eszköztől függően ide tartozik: LoRa rádió (SPI,
NSS, DIO1, BUSY, RESET, TX LED, PA / FEM vezérlés, rádió-táp), kijelző (OLED / TFT busz, reset, háttérvilágítás, táp), GPS
(RX/TX, engedélyezés, reset), felhasználói gomb, akku-mérés / -vezérlés, Vext és más tápkapcsolók, a kártya I²C-buszja,
USB, SPI flash/PSRAM, UART0, strapping lábak és (nRF) a 32 kHz-es kristály, a reset és az NFC lábak.

A repó variánsfájljaiból számolt szabad fejléc-lábak (ezeket kínálhatja fel a companion *Pinek* menüje, és ezek a fix repeater-lábak
jelöltjei):

| Eszköz | Szabad lábak |
|---|---|
| Heltec V3 | GPIO2, 4, 5, 6, 7, 33, 34, 38, 39, 40, 41, 42 |
| Heltec V4 (OLED) | GPIO4, 6, 33, 40, 41, 47, 48 (TFT változat és expansion kit: a GPIO3/4 I²C, ezért 6, 40, 41, 47, 48) |
| Heltec T114 | P0.28, P0.29, P0.30, P0.31, P1.01 (a pin-térkép csak részben ellenőrzött) |
| Heltec T096 | P0.02, P0.24, P0.29, P0.31, P1.00–P1.05, P1.07, P1.13 |

Jellemző alapértékek: DS18B20 `GPIO7` (V3) és `GPIO6` (V4, ahol a GPIO7 a FEM tápja); a T114-en és a T096-on nincs előre
beállított láb, a menüben kell választani.

## 5. Bekötés

- **DS18B20** (1–8 db ugyanarra a lábra): DATA → választott láb, VCC → 3V3, GND → GND, és **4,7 kΩ felhúzó ellenállás a DATA és a
  3V3 közé (kötelező)**. Parazita táp nem támogatott.
- **DHT11:** DATA → választott láb, VCC → 3V3, GND → GND; 4,7–10 kΩ felhúzó ajánlott (a belső felhúzót is bekapcsolja).
- **I²C szenzorok:** a kártya I²C-buszán (3V3-os eszközök). Két azonos című szenzor nem osztozhat egy buszon.

⚡ **Használj külső tápforrást** (közös GND-vel a kártyához) mindenhez, ami néhány mA-nél többet vesz fel: ~10 mA fölötti buzzer,
rezgőmotor (50–100 mA, tranzisztorral és szabadonfutó diódával), GPS modul, relé, SD-kártya, LED-szalag, sok DS18B20 hosszú
kábelen. Ilyen terhelést soha ne táplálj GPIO lábról. A kis szenzorok (DS18B20, DHT11, BME280, AHT, SHT, INA2xx) csak néhány
mA-t vesznek fel, a kártya 3V3-áról mehetnek. A Heltec V4 adatlapja 3,3 V-ra, Vext-re és 5 V-ra (USB) is 500 mA-t ad meg; a T114-hez
és a T096-hoz nincs közzétett kimeneti határ, ezért a kártya 3V3-át óvatosan terheld. Akkumulátoros vagy napelemes
repeatereknél minden további mA rövidíti az üzemidőt.

## 6. Fordítás

```bash
pip install platformio
./build.sh build-firmware Heltec_t096_companion_radio_ble     # eredmény: out/ (.bin / .uf2 / .zip)
# vagy egyszerűen:  pio run -e Heltec_v3_repeater
```

A `.github/workflows/heltec-release.yml` lefordítja a companion célokat, és `heltec-v*` tag-re (vagy kézzel az Actions fülről)
GitHub Release-t készít. Egységtesztek: `.github/workflows/run-unit-tests.yml`.

## 7. Útiterv — következő verzió

**Szenzortámogatás a repeaterekben** (V3, V4, T114, T096), szigorúan biztonságos lábakkal:

- DS18B20 és DHT11 minden repeater env-ben (nem csak a V3 + DHT11), **build-időben választott fix lábakon**.
- Egyetlen, eszközönkénti lábtábla. A hiba-lista helyett **whitelist** (egy láb csak akkor használható, ha ki van vezetve a
  fejlécre *és* a kártya nem használja). Ez a jelenlegi tiltólista ismert hiányait is pótolja: a T114 akku-mérése és -vezérlése
  (P0.04, P0.06), valamint a T096 akku-mérése (P0.03) és felhasználói gombja (P1.10) olyan makrókban van, amelyeket az ellenőrzés
  nem olvas.
- **Fordítási idejű ellenőrzés** (`static_assert`): ha egy repeater fix lába fedélzeti perifériával ütközik, a build nem fordul le.
- CLI a repeater szenzorainak olvasásához (lista, átnevezés, jelentési időköz), és opcionális jelentés telemetriaként vagy
  üzenetként az adminnak.
- Stabil telemetria-csatorna minden DS18B20 eszközhöz (jelenleg a csatornák a felismerés sorrendjében kapnak számot).
- Javasolt fix repeater-lábak (hardveren megerősítendők):

  | Eszköz | DS18B20 | DHT11 |
  |---|---|---|
  | V3 | GPIO7 | GPIO4 |
  | V4 | GPIO6 | GPIO47 |
  | T114 | P0.29 | P0.30 |
  | T096 | P0.29 | P0.31 |

## 8. Licenc

MIT, lásd a [`LICENSE`](LICENSE) fájlt. A HellCore a [MeshCore](https://github.com/meshcore-dev/MeshCore) és közreműködői munkájára épül (MIT licenc).
