# Panda Touch OSS

Open-source firmware for the BIGTREETECH PandaTouch (ESP32-S3, 5" 800×480).

Written from scratch — no reverse-engineered code from the official firmware.

## Why this exists

The official BTT firmware is closed-source and has known issues:

| Issue | Fix in this firmware |
|---|---|
| WiFi locks to one BSSID — won't roam between APs (#31, #360, #365) | `bssid_set = false`, `WIFI_ALL_CHANNEL_SCAN`, `sort by RSSI` |
| No battery indicator (#189) | Battery % + icon always visible in status bar |
| No ETA / finish time (#182) | ETA from Bambu MQTT `mc_remaining_time` shown on printer screen |
| WPA2/WPA3 mixed mode fails (#331) | `WIFI_AUTH_WPA2_PSK` + PMF capable |
| Screen freeze when WiFi lost (#365) | Non-blocking WiFi state machine, falls back to setup screen |

## Hardware

| Part | Spec |
|---|---|
| MCU | ESP32-S3R8 (dual-core 240 MHz, 8 MB PSRAM, 16 MB flash) |
| Display | 5" IPS 800×480 RGB parallel |
| Touch | GT911 (I2C) |
| Battery | LiPo 3.7 V, ~30 min use |
| WiFi | 802.11 b/g/n 2.4 GHz |

## Building

### Prerequisites
- [ESP-IDF v5.1+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- Python 3.8+

### Steps

```bash
git clone https://github.com/YOUR_USERNAME/panda-touch-oss
cd panda-touch-oss
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Pin mapping

Defined in `main/ui/ui_manager.hpp` under `namespace pins`. Adjust if your
hardware revision differs.

| Signal | GPIO |
|---|---|
| LCD PCLK | 42 |
| LCD HSYNC | 39 |
| LCD VSYNC | 41 |
| LCD DE | 40 |
| Touch SDA | 19 |
| Touch SCL | 20 |
| Touch INT | 18 |
| Touch RST | 38 |
| Backlight PWM | 2 |
| Battery ADC | 4 |
| Charge detect | 5 |

## Adding a printer

1. Go to home screen → tap **Add Printer**
2. Enter: printer IP, serial number, access code (found in printer settings → LAN mode)
3. Tap **Save** — the firmware connects via MQTT over TLS on port 8883

## Project structure

```
main/
├── main.cpp              — entry point, boot flow
├── app/
│   ├── config_manager    — NVS storage (WiFi, printers, settings)
│   ├── wifi_manager      — WiFi with SSID-based roaming
│   ├── bambu_client      — Bambu MQTT protocol (report parsing + commands)
│   ├── battery_monitor   — ADC voltage → % + charge state
│   └── printer_state.hpp — data model
└── ui/
    ├── ui_manager        — display driver (RGB), GT911 touch, LVGL init
    ├── screen_wifi       — WiFi setup with scan + keyboard
    ├── screen_home       — printer list with live status + battery bar
    └── screen_printer    — detail view: temps, progress, ETA, controls
```

## Bambu protocol notes

- MQTT over TLS, port 8883
- Username: `bblp`, password: access code
- Subscribe: `device/{serial}/report` (JSON push every ~1 s)
- Publish:   `device/{serial}/request`
- Printer uses a self-signed certificate — verification skipped (LAN-only)

## Contributing

PRs welcome. See open issues for the roadmap.

## License

MIT — see [LICENSE](LICENSE)
