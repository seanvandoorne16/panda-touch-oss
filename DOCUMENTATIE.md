# Panda Touch OSS — Technische Documentatie

> **Privédocument** — uitgebreide uitleg van de volledige codebase.  
> Geschreven voor: Sean van Doorne  
> Laatste update: juni 2026

---

## Inhoudsopgave

1. [Wat is dit project?](#1-wat-is-dit-project)
2. [Hardware overzicht](#2-hardware-overzicht)
3. [Softwarearchitectuur](#3-softwarearchitectuur)
4. [Bestandsstructuur](#4-bestandsstructuur)
5. [Configuratiebestanden](#5-configuratiebestanden)
6. [App-laag — de logica](#6-app-laag--de-logica)
7. [UI-laag — de schermen](#7-ui-laag--de-schermen)
8. [Hoofdprogramma (main.cpp)](#8-hoofdprogramma-maincpp)
9. [Hoe alles samenwerkt](#9-hoe-alles-samenwerkt)
10. [Bekende beperkingen](#10-bekende-beperkingen)
11. [Opgeloste bugs](#11-opgeloste-bugs)

---

## 1. Wat is dit project?

De **PandaTouch** is een aanraakscherm van BigTreeTech (BTT) dat je naast je Bambu Lab 3D-printer zet. Het verbindt via WiFi met de printer en laat je de printvoortgang zien, de printer bedienen (pauzeren, stoppen) en instellingen aanpassen.

Het **officiële firmware** van BTT is gesloten broncode — je kunt het niet inzien of aanpassen. Dit project schrijft die firmware **volledig opnieuw**, open source, met:
- Alle bekende bugs opgelost
- Batterij-indicator (ontbrak in origineel)
- ETA (resterende printtijd, ontbrak in origineel)
- Juiste WiFi-roaming (multi-AP netwerken werken nu correct)
- Schermbeveiliging met klok tijdens printen
- Slaapstand zonder crash-bij-wekken

Het draait op dezelfde hardware als de originele firmware: een **ESP32-S3** microcontroller met ESP-IDF v5 (het officiële Espressif framework).

---

## 2. Hardware overzicht

```
┌─────────────────────────────────────────────────────┐
│                    PandaTouch                        │
│                                                      │
│  ┌──────────────┐    ┌────────────┐                 │
│  │  ESP32-S3R8  │    │ 5" IPS LCD │  800×480 pixels │
│  │  240 MHz     │───▶│ RGB Parallel│                 │
│  │  8MB PSRAM   │    │ Interface  │                 │
│  │  16MB Flash  │    └────────────┘                 │
│  │              │    ┌────────────┐                 │
│  │              │───▶│   GT911    │  Aanraakscherm   │
│  │              │    │  (I2C)     │  capacitief      │
│  │              │    └────────────┘                 │
│  │              │    ┌────────────┐                 │
│  │              │───▶│  LiPo ADC  │  Batterijmeting  │
│  │              │    └────────────┘                 │
│  └──────────────┘                                   │
│         │                                           │
│    WiFi (802.11)                                    │
│         │                                           │
│    ─────▼──────────── LAN ───────────────────────  │
│         │                                           │
│    Bambu Lab Printer                                │
└─────────────────────────────────────────────────────┘
```

### Chips en componenten

| Component | Type | Verbinding |
|-----------|------|------------|
| Processor | ESP32-S3R8 (dual-core 240 MHz, 8 MB PSRAM) | — |
| Scherm | 5" IPS, 800×480 pixels | RGB parallel (16 datapinnen) |
| Touch | GT911 capacitief touchpanel | I2C (SDA=19, SCL=20) |
| Achtergrondlicht | PWM via LEDC | GPIO 2 |
| Batterij ADC | Spanningsdeler op ADC-pin | GPIO 4 (geschat) |
| Laad-detect | TP4056 charge-IC status | GPIO 5 (geschat) |

> **Let op:** GPIO-nummers zijn *geschat* van vergelijkbare BTT-hardware. Ze moeten geverifieerd worden tegen het echte PandaTouch schema voordat de firmware geflasht wordt.

---

## 3. Softwarearchitectuur

De software is opgebouwd in drie lagen:

```
┌─────────────────────────────────────────────────────┐
│                    UI-laag                          │
│  screen_home  screen_printer  screen_ams            │
│  screen_settings  screen_wifi  screen_screensaver   │
├─────────────────────────────────────────────────────┤
│                   App-laag                          │
│  BambuClient  WifiManager  ConfigManager            │
│  BatteryMonitor  SleepManager  ClockManager         │
│  ThumbnailFetcher                                   │
├─────────────────────────────────────────────────────┤
│                 Framework-laag                      │
│  ESP-IDF  FreeRTOS  LVGL  mbedTLS  MQTT             │
└─────────────────────────────────────────────────────┘
```

### Threading model

De ESP32-S3 heeft **twee CPU-cores**. De software verdeelt het werk zo:

```
Core 0 (main)          Core 1
──────────────         ─────────────────────
app_main()             lvgl_task()
WifiManager            lv_tick_inc(5ms)
BambuClient (MQTT)     lv_timer_handler()
ThumbnailFetcher       SleepManager::tick()
ClockManager (SNTP)
```

- **Core 0** doet alle netwerk- en applicatielogica
- **Core 1** doet de LVGL-grafische updates (elke 5ms)
- Een **recursive mutex** (`UiManager::lock/unlock`) beschermt LVGL-objecten tegen gelijktijdige toegang van beide cores
- Een **std::mutex** in `BambuClient` beschermt de printerstatus-data

---

## 4. Bestandsstructuur

```
panda-touch-oss/
├── CMakeLists.txt          ← Top-level buildbestand
├── partitions.csv          ← Flash-indeling
├── sdkconfig.defaults      ← ESP-IDF configuratie
├── idf_component.yml       ← Externe bibliotheken
├── DOCUMENTATIE.md         ← Dit bestand
└── main/
    ├── CMakeLists.txt      ← Bronbestanden + dependencies
    ├── main.cpp            ← Startpunt van de firmware
    ├── app/                ← Applicatielogica (geen UI)
    │   ├── printer_state.hpp       ← Data-model
    │   ├── config_manager.hpp/cpp  ← Instellingen opslaan (NVS)
    │   ├── wifi_manager.hpp/cpp    ← WiFi verbinding + roaming
    │   ├── bambu_client.hpp/cpp    ← MQTT communicatie met printer
    │   ├── battery_monitor.hpp/cpp ← Batterijspanning meten
    │   ├── sleep_manager.hpp/cpp   ← Slaapstand beheer
    │   ├── clock_manager.hpp/cpp   ← Klok (SNTP tijdsynchronisatie)
    │   └── thumbnail_fetcher.hpp/cpp ← Miniatuurafbeelding ophalen
    └── ui/                 ← Schermen (LVGL)
        ├── ui_manager.hpp/cpp      ← Scherm + touch initialisatie
        ├── screen_home.hpp/cpp     ← Hoofdscherm (printeroverzicht)
        ├── screen_printer.hpp/cpp  ← Detailscherm per printer
        ├── screen_ams.hpp/cpp      ← AMS filament-overzicht
        ├── screen_settings.hpp/cpp ← Instellingenscherm
        ├── screen_wifi.hpp/cpp     ← WiFi-instelling scherm
        └── screen_screensaver.hpp/cpp ← Schermbeveiliging
```

---

## 5. Configuratiebestanden

### `CMakeLists.txt` (top-level)
Het startpunt voor het buildsysteem (CMake). Zegt tegen ESP-IDF: "dit is een project genaamd `panda_touch_oss`."

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(panda_touch_oss)
```

### `partitions.csv`
Verdeelt de 16 MB Flash in stukken:

| Naam | Grootte | Gebruik |
|------|---------|---------|
| nvs | 24 KB | Opgeslagen instellingen (WiFi, helderheid, etc.) |
| phy | 4 KB | WiFi kalibratie data |
| factory | 3 MB | De firmware zelf |
| storage | 1 MB | SPIFFS bestandssysteem (reservegehouden) |

### `sdkconfig.defaults`
Alle ESP-IDF configuratie-opties voor dit project:

- `CONFIG_IDF_TARGET="esp32s3"` — we bouwen voor de ESP32-S3
- `CONFIG_SPIRAM_MODE_OCT=y` — PSRAM in octal mode (maximale snelheid)
- `CONFIG_COMPILER_OPTIMIZATION_PERF=y` — compiler optimaliseert op snelheid (-O2)
- `CONFIG_FREERTOS_HZ=1000` — FreeRTOS tikt elke milliseconde (nauwkeurige timers)
- `CONFIG_LV_MEM_SIZE_KILOBYTES=512` — 512 KB geheugen voor LVGL
- `CONFIG_MQTT_TASK_STACK_SIZE=8192` — 8 KB stack voor de MQTT-taak
- `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y` — TLS-certificaat niet verifiëren (Bambu printers gebruiken zelfondertekende certificaten)

### `idf_component.yml`
Externe bibliotheken die automatisch gedownload worden:
- **LVGL ≥ 8.3.0** — de grafische bibliotheek voor het scherm
- **esp_lcd_touch_gt911** — driver voor de GT911 touch-chip

### `main/CMakeLists.txt`
Registreert alle `.cpp` bronbestanden en externe afhankelijkheden:
- `nvs_flash` — opslaan in NVS (niet-vluchtig geheugen)
- `esp_wifi` — WiFi driver
- `mqtt` — MQTT protocol client
- `esp_tls` — TLS/SSL encryptie (voor MQTT en FTPS)
- `esp_lcd`, `esp_lcd_touch_gt911` — scherm en touch drivers
- `esp_adc` — ADC voor batterijmeting
- `lvgl` — grafische bibliotheek
- `esp_sntp` — tijdssynchronisatie via internet

---

## 6. App-laag — de logica

### `printer_state.hpp` — Het datamodel

Dit bestand definieert alle datastructuren die de staat van een printer beschrijven. Het is **het hart van het systeem** — alle andere bestanden gebruiken deze structuren.

```
PrinterState
├── Identificatie
│   ├── serial          (uniek serienummer, bijv. "01S00C123456789")
│   ├── name            (gebruikersnaam, bijv. "Mijn X1C")
│   ├── ip              (IP-adres op het LAN)
│   └── access_code     (wachtwoord voor API-toegang)
│
├── Verbindingsstatus
│   ├── online          (true = verbonden via MQTT)
│   └── last_seen_ms    (tijdstempel laatste bericht, milliseconden)
│
├── Printstatus
│   ├── status          (UNKNOWN/IDLE/PRINTING/PAUSED/FINISHED/FAILED/OFFLINE)
│   ├── gcode_file      (bestandsnaam op printer)
│   ├── subtask_name    (naam van de huidige afdruktaak)
│   ├── progress_pct    (voortgang 0-100%)
│   ├── remaining_sec   (resterende seconden)
│   ├── layer_cur       (huidige laag)
│   └── layer_total     (totaal aantal lagen)
│
├── Temperaturen
│   ├── nozzle_temp / nozzle_target   (nozzle temperatuur in °C)
│   ├── bed_temp / bed_target         (bedtemperatuur in °C)
│   └── chamber_temp                  (kamertemperatuur in °C)
│
├── Ventilatoren (0-100%)
│   ├── fan_part_cooling
│   ├── fan_aux
│   └── fan_chamber
│
├── Verlichting
│   ├── chamber_light   (kamerverlichting aan/uit)
│   └── work_light      (werklamp aan/uit)
│
├── AMS (Automatisch filamentwisselaar)
│   └── ams_trays[]     (lijst van AMS-trays met slots)
│       └── slots[]     (kleur, materiaal, resterende %)
│
└── remaining_str()     (geeft "1u 23m" terug voor de UI)
```

**Waarom zo gedetailleerd?** De Bambu MQTT API stuurt alle printerstatus als JSON. Dit datamodel mappt precies op die JSON-velden, zodat de UI altijd een complete snapshot heeft.

---

### `config_manager.hpp/cpp` — Instellingen opslaan

Slaat gebruikersinstellingen permanent op in de **NVS** (Non-Volatile Storage) — het flash-geheugen dat data behoudt zelfs als de stroom uitvalt.

**Wat wordt opgeslagen:**

| Sleutel | Type | Standaard | Beschrijving |
|---------|------|-----------|-------------|
| `wifi_ssid` | string | — | WiFi-netwerknaam |
| `wifi_pass` | string | — | WiFi-wachtwoord |
| `printers` | JSON string | `[]` | Lijst van printers (serienummer, IP, access code, naam) |
| `brightness` | uint8 | 80 | Schermhelderheid 1-100% |
| `sleep_sec` | uint16 | 300 | Slaapstand na X seconden (0 = nooit) |

**Hoe het werkt:**
```
ConfigManager::instance()   ← Singleton (maar één instantie)
    .init()                 ← Opent NVS-partitie
    .get_wifi()             ← Leest WiFi-credentials
    .save_wifi(cfg)         ← Schrijft WiFi-credentials
    .get_printers()         ← Leest printerlijst als vector
    .save_printers(list)    ← Schrijft als JSON-string
    .get_brightness()       ← Leest helderheid (1-100)
    .save_brightness(v)     ← Schrijft helderheid
```

**Bugfix M7 — NVS null terminator:**
ESP-IDF's `nvs_get_str` slaat soms een `\0` (nulterminator) op aan het einde van strings. Als je die niet verwijdert, krijg je strings met een onzichtbaar karakter aan het einde. De code doet dit:
```cpp
if (!out.empty() && out.back() == '\0') out.pop_back();
```

---

### `wifi_manager.hpp/cpp` — WiFi verbinding

Beheert de WiFi-verbinding met ondersteuning voor **multi-AP roaming** (meerdere accesspoints op hetzelfde netwerk).

**Het grote probleem met de originele firmware:**
De originele BTT-firmware verbond altijd met een **specifiek MAC-adres** (`bssid_set = true`). Als je drie accesspoints hebt met dezelfde SSID, zag de PandaTouch drie aparte netwerken en switchte nooit automatisch naar een sterkere.

**De fix:**
```cpp
wcfg.sta.bssid_set   = false;              // Verbind op SSID, niet op MAC
wcfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;    // Scan alle kanalen
wcfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL; // Kies sterkste AP
esp_wifi_set_rssi_threshold(-80);          // Herverbind bij < -80 dBm signaal
```

**Na verbinding — modem sleep:**
```cpp
esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // Bespaart ~20 mA wanneer verbonden
```
De WiFi-chip slaapt tussen ontvangstvensters door. De verbinding blijft actief, maar het stroomverbruik daalt.

**Multi-observer patroon (bugfix H3):**
De originele code had maar één callback-slot voor WiFi-statuswijzigingen. Als twee stukken code allebei wilden weten of WiFi verbonden was, overschreef de tweede de eerste. Nu:
```cpp
std::vector<WifiStateCallback> _callbacks;   // Meerdere luisteraars
void add_state_listener(WifiStateCallback cb); // Voeg toe
void clear_state_listeners();                  // Wis allemaal
```

**WiFi-scan:**
`scan_async(callback)` start een asynchrone scan van alle zichtbare netwerken. Wanneer klaar, roept het de callback aan met een lijst van unieke SSID's (gesorteerd op signaalsterkte, duplicaten verwijderd).

---

### `bambu_client.hpp/cpp` — Communicatie met de printer

Dit is de **kern van de firmware** — het praat met de Bambu Lab printer via **MQTT over TLS**.

#### Hoe Bambu Lab communicatie werkt

Bambu Lab printers draaien een ingebouwde MQTT-broker. De PandaTouch verbindt als MQTT-client:

```
PandaTouch                        Bambu Printer
    │                                  │
    │──── TLS verbinding poort 8883 ──▶│
    │                                  │
    │  Credentials:                    │
    │    username: "bblp"              │
    │    password: <access_code>       │
    │    client_id: "pt_<serienummer>" │
    │                                  │
    │──── SUBSCRIBE ──────────────────▶│
    │     topic: device/<sn>/report    │
    │                                  │
    │──── PUBLISH ────────────────────▶│
    │     topic: device/<sn>/request   │
    │     payload: {"pushing":{"command":"pushall"}}
    │                                  │
    │◀─── JSON status updates (1 Hz) ──│
    │     {"print":{"gcode_state":"RUNNING",
    │               "mc_percent":42, ...}}
```

#### JSON-parsing

De printer stuurt status-updates als JSON. De `parse_print()` methode leest de relevante velden:

| JSON-veld | PrinterState-veld | Beschrijving |
|-----------|-------------------|-------------|
| `gcode_state` | `status` | "RUNNING" → PRINTING, "PAUSE" → PAUSED, etc. |
| `mc_percent` | `progress_pct` | Voortgang 0-100% |
| `mc_remaining_time` | `remaining_sec` | Resterende tijd in **minuten** (×60 = seconden) |
| `layer_num` | `layer_cur` | Huidige laagnummer |
| `total_layer_num` | `layer_total` | Totaal lagen |
| `nozzle_temper` | `nozzle_temp` | Nozzle temperatuur |
| `bed_temper` | `bed_temp` | Bed temperatuur |
| `spd_lvl` | `print_speed_pct` | Snelheidsniveau 1-4 → 50/100/124/166% |
| `cooling_fan_speed` | `fan_part_cooling` | Ventilatorsnelheid (0-15 → 0-100%) |

**Bugfix M3 — Foutieve snelheidsconversie:**
De originele code las `spd_lvl` drie keer uit met verkeerde standaardwaarden. Nu:
```cpp
int spd = json_int(p, "spd_lvl", 2);  // Eén keer lezen, standaard = 2 (normaal)
_state.print_speed_pct = spd == 1 ? 50 : spd == 2 ? 100 : spd == 3 ? 124 : 166;
```

#### Delta-state optimalisatie

Bambu printers sturen ook "heartbeat" berichten zonder print/ams data. Zonder optimalisatie kopieert de code bij elk bericht de volledige `PrinterState` (~200 bytes met vectors) en triggert een UI-update. Met de fix:
```cpp
bool changed = false;
if (print) { parse_print(print); changed = true; }
if (ams)   { parse_ams(ams);     changed = true; }
if (!changed) return;  // Sla heartbeats over
```

#### Commando's sturen

De printer accepteert commando's als JSON op het request-topic:

```cpp
pause_print()   → {"print":{"command":"pause"}}
resume_print()  → {"print":{"command":"resume"}}
stop_print()    → {"print":{"command":"stop"}}
set_light(...)  → {"system":{"command":"ledctrl","led_node":"chamber_light","led_mode":"on"}}
set_speed(pct)  → {"print":{"command":"print_speed","param":"2"}}
set_fan(...)    → {"print":{"command":"print_option","cooling_fan_speed":255}}
home_axis(...)  → {"print":{"command":"gcode_line","param":"G28 X Y\n"}}
move_axis(...)  → {"print":{"command":"gcode_line","param":"G91\nG1 X10 F3000\nG90\n"}}
send_gcode(...) → {"print":{"command":"gcode_line","param":"<gcode>"}}
```

**Thread-veiligheid (bugfix C5):**
De MQTT-callback loopt op Core 0, de UI loopt op Core 1. Zonder bescherming zouden ze tegelijkertijd `_state` kunnen lezen/schrijven → dataraces → crashes. De fix:
```cpp
mutable std::mutex _state_mutex;
// Bij schrijven (parse):
std::lock_guard<std::mutex> lock(_state_mutex);
// Bij lezen (state()):
std::lock_guard<std::mutex> lock(_state_mutex);
return _state;  // geeft een kopie terug (buiten de lock)
```

---

### `battery_monitor.hpp/cpp` — Batterijmeting

Meet de spanning van de LiPo-batterij via de ADC (Analog-to-Digital Converter) en geeft een percentage en laadstatus terug.

**Hoe het werkt:**

```
LiPo batterij (3.0–4.2V)
        │
    Spanningsdeler (×2)
        │
    ADC-pin (0–3.9V range)
        │
    ESP32-S3 ADC (12-bit, 0–4095)
        │
    voltage_to_pct() lookup table
        │
    0–100% batterijpercentage
```

**Bugfix H5 — Hardgecodeerd ADC-kanaal:**
De ADC-pin verschilt per hardware-revisie. Eerder was kanaal `ADC_CHANNEL_3` hardgecodeerd, ook al werd een GPIO-nummer meegegeven als parameter. De fix:
```cpp
adc_unit_t unit_id;
adc_channel_t chan;
adc_oneshot_io_to_channel(adc_gpio, &unit_id, &chan);  // GPIO → juiste kanaal
```

**LiPo discharge curve:**
LiPo-batterijen zijn niet lineair — 50% spanning is niet 50% capaciteit. De lookup-tabel:

| Spanning (mV) | Capaciteit (%) |
|---------------|----------------|
| 4200 | 100 |
| 4000 | 80 |
| 3800 | 60 |
| 3700 | 50 |
| 3500 | 30 |
| 3300 | 10 |
| 3000 | 0 |

**LVGL-symbolen:**
De methode `lv_symbol()` geeft het juiste batterij-icoon terug voor de statusbalk:
- Opladen → `LV_SYMBOL_CHARGE` (bliksem)
- ≥ 80% → `LV_SYMBOL_BATTERY_FULL`
- 50-80% → `LV_SYMBOL_BATTERY_3`
- 20-50% → `LV_SYMBOL_BATTERY_2`
- < 20% → `LV_SYMBOL_BATTERY_1`
- Leeg → `LV_SYMBOL_BATTERY_EMPTY`

---

### `sleep_manager.hpp/cpp` — Slaapstand

Beheert wanneer het scherm dimmt. **Kernbeslissing:** we gebruiken NOOIT `esp_deep_sleep()` — alleen het achtergrondlicht dimmen naar 1%.

**Waarom geen deep sleep?**
De originele BTT firmware gebruikte deep sleep. Dit veroorzaakte drie ernstige bugs (#228, #260, #328 — samen 74 reacties op GitHub):
- Bij het wekken moest het scherm opnieuw geïnitialiseerd worden
- Dat veroorzaakte een race condition → crash
- WiFi moest ook opnieuw verbinden → vertraging van 10+ seconden

**Onze aanpak:**
```
Scherm inactief X seconden
        ↓
set_brightness(1%)   ← "Scherm uit" (bijna niet zichtbaar)
WiFi: blijft verbonden
MQTT: blijft verbinden
        ↓
Aanraking gedetecteerd (touch callback)
        ↓
set_brightness(g_brightness)  ← "Scherm aan" (direct, geen herstart)
```

**Twee aparte timeouts (bugfix #59 en #268):**
- `idle_timeout_sec` — na hoelang het scherm dimt bij inactiviteit
- `print_timeout_sec` — apart timeout tijdens het printen (standaard: nooit)

Zo dim je het scherm 's nachts wel als je niets doet, maar niet terwijl de printer bezig is.

---

### `clock_manager.hpp/cpp` — Klok

Synchroniseert de tijd via **SNTP** (Simple Network Time Protocol) — vergelijkbaar met hoe je computer de tijd bijhoudt via internet.

**Hoe het werkt:**
1. Wacht tot WiFi verbonden is
2. Stuurt een tijdsverzoek naar `pool.ntp.org`
3. ESP32 stelt zijn interne klok in op de ontvangen UTC-tijd
4. Tijdzone-instelling (bijv. `CET-1CEST,M3.5.0,M10.5.0/3`) zet UTC om naar lokale tijd

**Twee output-methoden:**
- `time_str()` → `"14:35"` (voor statusbalk en screensaver)
- `date_str()` → `"Sat 13 Jun"` (voor de statusbalk)

**Standaard tijdzone:** CET (Centraal-Europese Tijd), inclusief zomertijd.

---

### `thumbnail_fetcher.hpp/cpp` — Miniatuurafbeelding ophalen

Haalt het miniatuurplaatje van het bestand dat momenteel geprint wordt op, zodat de UI dit kan tonen.

**Protocol: FTPS (FTP over impliciete TLS)**

Bambu printers draaien een FTP-server met TLS-encryptie op poort 990. De verbinding werkt zo:

```
PandaTouch                          Bambu Printer (:990)
    │                                       │
    │──── TLS verbinding (poort 990) ──────▶│
    │◀─── 220 Welkomstbericht ──────────────│
    │──── USER bblp ────────────────────────▶│
    │◀─── 331 Wachtwoord nodig ─────────────│
    │──── PASS <access_code> ───────────────▶│
    │◀─── 230 Ingelogd ─────────────────────│
    │──── TYPE I (binair mode) ─────────────▶│
    │──── PASV (passieve modus) ────────────▶│
    │◀─── 227 (192,168,1,100,8,5) ──────────│  ← data poort = 8*256+5 = 2053
    │                                       │
    │──── TLS verbinding (poort 2053) ──────▶│  ← datakanaal
    │                                       │
    │──── RETR /cache/<taak>/thumbnail/plate_1.png ▶│
    │◀─── 150 Opening data channel ─────────│
    │◀─── [PNG data via datakanaal] ────────│
    │◀─── 226 Transfer Complete ────────────│
    │──── QUIT ─────────────────────────────▶│
```

**Asynchrone werking:**
De fetch draait in een aparte FreeRTOS-taak zodat de UI niet blokkeert:
```cpp
fetch(ip, access_code, subtask, [](const std::vector<uint8_t>& png) {
    // Callback wanneer het plaatje beschikbaar is
    // png is leeg als het ophalen mislukt
});
```

> **Onbevestigd:** het pad `/cache/<subtask>/thumbnail/plate_1.png` is gedocumenteerd door de community maar niet geverifieerd op echte hardware.

---

## 7. UI-laag — de schermen

### `ui_manager.hpp/cpp` — Scherm en touch initialisatie

Initialiseert alle hardware voor het scherm en beheert de LVGL-mutex.

**Drie initialisatiestappen:**

**1. LCD init (`init_lcd`):**
Configureert het RGB-parallelinterface naar het 5"-scherm. Gebruikt **double buffering** (twee framebuffers in PSRAM) — terwijl de ene buffer op het scherm getoond wordt, tekent LVGL in de andere. Dit voorkomt "tearing" (zichtbare scheuren bij bewegende UI-elementen).

```
PSRAM Framebuffer 1 (800×480×2 = 768 KB)  ← LVGL tekent hier
PSRAM Framebuffer 2 (800×480×2 = 768 KB)  ← Scherm toont dit
                                              (wisselen bij vsync)
```

**2. Touch init (`init_touch`):**
Configureert de GT911 touch-chip via I2C. Elke 5ms leest `lvgl_touch_cb` de huidige aanraakcoördinaten en stuurt die naar LVGL.

**Bugfix H2 — Slaapstand werd nooit gewekt:**
Bij elke aanraking wordt nu ook `SleepManager::touch_activity()` aangeroepen:
```cpp
if (pressed) SleepManager::instance().touch_activity();  // Reset de sleep-timer
```

**3. LVGL init (`init_lvgl`):**
- Alloceert twee renderingbuffers in PSRAM (20 rijen × 800 pixels)
- Registreert de flush-callback (stuurt getekende pixels naar het scherm)
- Maakt de recursive mutex aan (bugfix C1)

**Bugfix C1 — Deadlock door niet-recursieve mutex:**
`app_main` nam de mutex, riep `ScreenHome::create()` aan, die riep `update_status_bar()` aan, die probeerde de mutex opnieuw te nemen → deadlock. Fix: `xSemaphoreCreateRecursiveMutex()` staat dezelfde taak toe de mutex meerdere keren te nemen.

---

### `screen_home.hpp/cpp` — Hoofdscherm

Het eerste scherm dat je ziet na verbinden. Toont een kaart voor elke geconfigureerde printer.

**Lay-out:**
```
┌─────────────────────────────────────────────────────┐
│  🔋 78%   📶 192.168.1.55   🕐 14:35  Sat 13 Jun  │  ← Statusbalk
├─────────────────────────────────────────────────────┤
│                                                     │
│  ┌──────────────────┐  ┌──────────────────┐        │
│  │  Mijn X1C        │  │  Bambu P1S       │        │
│  │  ██████░░ 67%    │  │  IDLE            │        │
│  │  1u 23m resterend│  │                  │        │
│  │  [PRINTING]      │  │  [OFFLINE]       │        │
│  └──────────────────┘  └──────────────────┘        │
│                                                     │
│                          ⚙ Instellingen             │
└─────────────────────────────────────────────────────┘
```

**Printerkaarten:**
Voor elke printer in de configuratie wordt een `BambuClient` aangemaakt en verbonden. De kaart toont live-status via `add_update_listener()` — elke keer als de printer een update stuurt, wordt de kaart bijgewerkt.

**Bugfix H1 — Scherm/MQTT-lek:**
Als `create()` werd aangeroepen terwijl het scherm al bestond (bijv. na terugkeer van het instellingenscherm), werd het oude scherm niet opgeruimd. Fix:
```cpp
void ScreenHome::create() {
    if (s_screen) destroy();  // Altijd eerst opruimen
    // ...
}
```

**Bugfix C4 — Use-after-free in event callback:**
Printer-serienummers werden als `char*` pointer naar een tijdelijke string doorgegeven aan LVGL-event callbacks. Tegen de tijd dat de gebruiker tikte, was die string al vernietigd. Fix: heap-allocatie met automatisch opruimen:
```cpp
auto* serial_key = new std::string(cfg.serial);  // Leeft zo lang als de kaart
lv_obj_add_event_cb(card, handler, LV_EVENT_CLICKED, serial_key);
lv_obj_add_event_cb(card, [](lv_event_t* e) {    // Verwijder bij weggooien kaart
    delete static_cast<std::string*>(lv_event_get_user_data(e));
}, LV_EVENT_DELETE, serial_key);
```

---

### `screen_printer.hpp/cpp` — Printerdetailscherm

Toont alle details van één printer en geeft bedieningsknoppen.

**Lay-out:**
```
┌─────────────────────────────────────────────────────┐
│  ← Terug                   Mijn X1C                │
├─────────────────────────────────────────────────────┤
│  [Miniatuur]   benchy.3mf                          │
│                Laag 45 / 120                        │
│  ████████░ 67%   🕐 1u 23m                         │
├─────────────────────────────────────────────────────┤
│  Nozzle: 220°C → 220°C    Bed: 65°C → 65°C        │
│  Kamer: 32°C               Snelheid: 100%           │
├─────────────────────────────────────────────────────┤
│  [⏸ Pauze]  [▶ Hervatten]  [⏹ Stop]  [💡 Licht]  │
│                                        [📦 AMS]    │
└─────────────────────────────────────────────────────┘
```

**Bugfix M1 — LVGL knoppen uitschakelen:**
De originele code gebruikte de verkeerde API om knoppen te grijzen. Juist:
```cpp
lv_obj_add_state(btn, LV_STATE_DISABLED);    // Uitschakelen
lv_obj_clear_state(btn, LV_STATE_DISABLED);  // Inschakelen
```

---

### `screen_ams.hpp/cpp` — AMS filamentoverzicht

Toont de AMS (Automatische filamentwisselaar) met per slot het resterende filamentpercentage.

**Lay-out per AMS-tray:**
```
┌──────────────────────────────────────┐
│  AMS 1    Vochtigheid: 2   Temp: 25°C│
│                                      │
│  ●1  PLA-CF    ████████░░  83%  [GROEN]  │
│  ●2  ABS       ████░░░░░░  43%  [ORANJE] │
│  ●3  PETG      █░░░░░░░░░  11%  [ORANJE] │
│  ●4  TPU       ░░░░░░░░░░   3%  [ROOD]   │
└──────────────────────────────────────┘
```

Kleurcodering voortgangsbalk:
- > 30% → groen
- 10-30% → oranje
- ≤ 10% → rood (bijna leeg)

**Bugfix C3 — Null-pointer crash bij terugnavigeren:**
Als je vanuit het AMS-scherm terugging, werd `s_client` op null gezet vóór de navigatie-check. Fix:
```cpp
auto client = std::move(s_client);  // Sla op
s_client = nullptr;                 // Reset
if (client) ScreenPrinter::show(client);  // Navigeer met opgeslagen waarde
else ScreenHome::create();
```

---

### `screen_settings.hpp/cpp` — Instellingenscherm

Scrollbaar scherm met alle gebruikersinstellingen.

**Secties:**

**1. Display**
- Helderheidsschuifregelaar (1-100%)
- Slaat op in NVS via `ConfigManager`
- Past direct het achtergrondlicht aan via `UiManager::set_brightness()`

**2. Slaapstand**
- *Inactief scherm uit na:* Nooit / 1min / 2min / 5min / 10min / 30min
- *Scherm uit tijdens print:* Aparte instelling (standaard: nooit)

**3. Klok**
- Tijdzone dropdown: UTC, CET, EET, GMT, EST, CST, MST, PST, JST, AEST
- Stelt de omgevingsvariabele `TZ` in en roept `tzset()` aan

**4. Schermvergrendeling**
- Toggle: lang indrukken (2 sec) vergrendelt het scherm
- (Implementatie van de daadwerkelijke vergrendeling is voorbehouden)

---

### `screen_wifi.hpp/cpp` — WiFi-instelling

Wordt getoond bij eerste opstart of als WiFi mislukt.

**Stroom:**
```
Scherm opent → Auto-scan start
    ↓
Scan klaar → Dropdown met gevonden netwerken
    ↓
Gebruiker selecteert SSID + vult wachtwoord in
    ↓
"Verbinden" → Spinner zichtbaar
    ↓
Verbonden → "Verbonden: 192.168.1.55" → 0,8s wachten → Naar Home-scherm
```

**Op-scherm toetsenbord:**
LVGL heeft een ingebouwd toetsenbord-widget (`lv_keyboard_create`). Dit is gekoppeld aan het wachtwoordveld zodat de gebruiker het wachtwoord kan intypen op het aanraakscherm.

**Bugfix H3 — Callback werd overschreven:**
Bij verbinden werd één globale WiFi-callback overschreven. Als `main.cpp` al een callback had ingesteld (voor SNTP-sync), ging die verloren. Fix:
```cpp
WifiManager::instance().clear_state_listeners();  // Wis bestaande listeners
WifiManager::instance().add_state_listener(on_wifi_state);  // Voeg toe
```

---

### `screen_screensaver.hpp/cpp` — Schermbeveiliging

Wordt getoond terwijl de printer bezig is en de gebruiker het scherm laat dimmen.

**Lay-out:**
```
┌─────────────────────────────────────────────────────┐
│                                                     │
│                    14:35                            │  ← Grote klok (48pt)
│                                                     │
│         benchy_final_v3.3mf                        │
│                                                     │
│         ████████████░░░░░  67%                     │
│         67%          🕐 1u 23m                      │
│                                                     │
│                  Tik om te wekken                  │
└─────────────────────────────────────────────────────┘
```

Aanraking → `dismiss()` → sleepmanager gewekt → terug naar printer-scherm.

**Bugfix M5 — Klok-timer te kort:**
De klok werkt met HH:MM formaat (verandert elke minuut). De update-timer stond op 30 seconden — zinloos want de weergave verandert toch maar elke minuut. Gecorrigeerd naar 60000ms.

---

## 8. Hoofdprogramma (main.cpp)

`app_main()` is het startpunt van de firmware — de ESP32 roept dit automatisch aan na het opstarten.

**Opstartvolgorde:**

```
1. ConfigManager::init()
   └─ NVS openen, WiFi-credentials en printerlijst laden

2. UiManager::init()
   ├─ LCD initialiseren (RGB parallel, double buffer)
   ├─ Touch initialiseren (GT911 via I2C)
   └─ LVGL initialiseren (buffers, mutex)

3. BatteryMonitor::init()
   └─ ADC configureren, eerste meting uitvoeren

4. SleepManager::init()
   └─ Timeouts instellen vanuit configuratie

5. WifiManager::init()
   └─ WiFi-stack starten

6. WiFi-callbacks registreren
   ├─ Bij verbonden: ClockManager::init() (SNTP starten)
   └─ Bij mislukt: WiFi-instelling scherm tonen

7. LVGL-taak starten (Core 1)
   └─ lv_tick_inc(5ms) + lv_timer_handler() elke 5ms

8. Eerste scherm bepalen
   ├─ Geen opgeslagen WiFi → WiFi-instelling scherm
   └─ Opgeslagen WiFi → Verbinden + Home-scherm
```

**De LVGL-taak:**
```cpp
static void lvgl_task(void*) {
    uint32_t sleep_counter = 0;
    for (;;) {
        lv_tick_inc(5);                          // Vertel LVGL: 5ms verstreken
        UiManager::instance().tick();            // LVGL verwerkt timers en events
        if (++sleep_counter >= 200) {            // Elke 200 × 5ms = 1 seconde
            sleep_counter = 0;
            SleepManager::instance().tick();     // Controleer slaap-timeout
        }
        vTaskDelay(pdMS_TO_TICKS(5));            // Wacht 5ms
    }
}
```

**Globale helderheid (bugfix M4):**
```cpp
uint8_t g_brightness = 80;  // Gedefinieerd in main.cpp
```
Vroeger stond dit in een UI-bestand. Dat zorgde voor een omgekeerde afhankelijkheid (een UI-bestand werd geïmporteerd door het app-bestand). Nu staat het correct in `main.cpp` en wordt het gedeeld via `extern uint8_t g_brightness`.

---

## 9. Hoe alles samenwerkt

**Scenario: Gebruiker kijkt naar printvoortgang**

```
Bambu Printer
    │ MQTT JSON update (elke seconde)
    ▼
BambuClient::parse_report()
    │ Lock _state_mutex
    │ parse_print() → update _state
    │ Unlock
    │ changed = true → notificeer callbacks
    ▼
PrinterStateCallback (in screen_printer.cpp)
    │ UiManager::lock()  ← Neem LVGL mutex
    │ lv_label_set_text(progress_lbl, "67%")
    │ lv_bar_set_value(progress_bar, 67)
    │ lv_label_set_text(eta_lbl, "1u 23m")
    │ UiManager::unlock()
    ▼
LVGL task (Core 1, elke 5ms)
    │ lv_timer_handler()
    │ → detecteert gewijzigde widgets
    │ → tekent in backbuffer
    │ → swap framebuffers
    ▼
LCD scherm toont bijgewerkte UI
```

**Scenario: Gebruiker raakt scherm aan na slaapstand**

```
GT911 touch chip
    │ I2C interrupt
    ▼
UiManager::lvgl_touch_cb()
    │ esp_lcd_touch_get_coordinates()
    │ pressed = true
    │ SleepManager::touch_activity()  ← Reset slaap-timer
    │ UiManager::set_brightness(g_brightness)  ← Scherm aan
    ▼
LVGL verwerkt aanraking
    │ Bepaalt welk widget geraakt is
    │ Stuurt LV_EVENT_CLICKED
    ▼
Event callback (in het actieve scherm)
```

---

## 9b. OTA Firmware-update

### Hoe het werkt

OTA (Over-The-Air) laat de firmware zichzelf bijwerken via WiFi — zonder USB-kabel of computer.

**Flash-indeling voor OTA:**
```
nvs      (24 KB)  — instellingen
phy      (4 KB)   — WiFi kalibratie
otadata  (8 KB)   — bijhoudt welke slot actief is
ota_0    (3 MB)   — firmware slot A  ← actief
ota_1    (3 MB)   — firmware slot B  ← update gaat hier naartoe
storage  (~10 MB) — SPIFFS bestanden
```

ESP-IDF schrijft de nieuwe firmware naar de **inactieve slot** terwijl de huidige firmware gewoon doorloopt. Pas bij herstarten wordt overgeschakeld. Als de nieuwe firmware crasht bij het opstarten, springt de bootloader automatisch terug naar de vorige werkende versie (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`).

**Updatestroom:**
```
Instellingen → "Bijwerken" knop
        ↓
ScreenOta::show()
        ↓
OtaManager::start_update(FIRMWARE_OTA_URL, ...)
        ↓ (aparte FreeRTOS-taak)
esp_https_ota_begin()       — verbinding maken met GitHub
esp_https_ota_perform()     — download + flash naar ota_1 (voortgangsbalk 0-100%)
esp_https_ota_finish()      — markeer ota_1 als geldig
esp_restart()               — herstart → bootloader kiest ota_1
```

**GitHub Releases instellen (eenmalig):**
1. Bouw de firmware: `idf.py build`
2. Het binaire bestand staat in: `build/panda_touch_oss.bin`
3. Ga naar GitHub → Releases → "Create a new release"
4. Tag: `v1.0.1`, upload `panda_touch_oss.bin`
5. De URL in `version.hpp` werkt automatisch voor de nieuwste release

**Bestanden:**
- `main/app/version.hpp` — versienummer + download-URL
- `main/app/ota_manager.hpp/cpp` — download + flash logica
- `main/ui/screen_ota.hpp/cpp` — het update-scherm

---

## 10. Bekende beperkingen

### GPIO-nummers niet geverifieerd
De pin-nummers in `ui_manager.hpp` zijn geschat van vergelijkbare BTT-hardware:
```cpp
constexpr int LCD_PCLK = 42;   // GESCHAT
constexpr int TOUCH_SDA = 19;  // GESCHAT
constexpr int BAT_ADC = 4;     // GESCHAT
```
**Actie:** Vergelijk met de officiele PandaTouch schematic (BTT of community) voordat je flasht.

### Thumbnail-pad niet geverifieerd op hardware
```
/cache/<subtask_name>/thumbnail/plate_1.png
```
Dit pad is gedocumenteerd door community-projecten (ha-bambulab), maar niet getest op een echte PandaTouch → echte Bambu printer verbinding. Als het mislukt, zijn de thumbnails leeg maar crasht de firmware niet.

### MQTT-commando's niet getest op echte printer
Commando's als `pause`, `resume`, `stop` zijn gebaseerd op reverse-engineering door de community. Ze zijn plausibel maar kunnen afwijken per printermodel of firmwareversie.

### Nooit gecompileerd of geflasht
De volledige codebase is geschreven maar nog niet gebouwd met ESP-IDF. Compilatiefouten zijn mogelijk, met name:
- API-wijzigingen tussen ESP-IDF versies
- Header-includes die ontbreken
- TLS-configuratie-velden die anders heten in esp_tls v5.x

---

## 11. Opgeloste bugs

Alle bugs zijn gebaseerd op gerapporteerde GitHub issues van de originele BTT firmware.

| Code | Ernst | Beschrijving | Fix |
|------|-------|-------------|-----|
| C1 | Kritiek | Deadlock bij nested LVGL lock | Recursive mutex |
| C2 | Kritiek | Dangling pointer in MQTT client-ID | `std::string _client_id` als member |
| C3 | Kritiek | Null-pointer crash bij AMS terugnavigatie | `std::move()` voor reset |
| C4 | Kritiek | Use-after-free in LVGL event callback | Heap-allocatie met delete-callback |
| C5 | Kritiek | Data race tussen MQTT-taak en UI-taak | `std::mutex` op PrinterState |
| H1 | Hoog | Scherm/MQTT-lek bij herhaald aanmaken | Guard `if (s_screen) destroy()` |
| H2 | Hoog | Slaapstand werd nooit gewekt door aanraking | `touch_activity()` in touch callback |
| H3 | Hoog | WiFi-callback werd overschreven | Multi-observer vector |
| H4 | Hoog | FetchArgs geheugenlek bij FreeRTOS OOM | Check `xTaskCreate` resultaat |
| H5 | Hoog | Hardgecodeerd ADC-kanaal | `adc_oneshot_io_to_channel()` |
| M1 | Midden | LVGL knoppen uitschakelen API fout | `lv_obj_add_state(LV_STATE_DISABLED)` |
| M2 | Midden | Printernaam werd nooit opgeslagen/getoond | `name` parameter toegevoegd aan constructor |
| M3 | Midden | Foutieve snelheidsconversie `spd_lvl` | Eén correcte read met juiste standaard |
| M4 | Midden | `g_brightness` in verkeerd bestand | Verplaatst naar `main.cpp` |
| M5 | Midden | Klok-timer 30s i.p.v. 60s | Timer aangepast naar 60000ms |
| M7 | Midden | NVS strings met nulterminator | Strip `\0` na `nvs_get_str` |

---

*Einde documentatie*
