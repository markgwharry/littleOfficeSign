# Office Door Sign (ESP32 + MQTT + NAS Integration)

An integrated system that displays your Microsoft 365 calendar status ("Free" or "Busy") on a small touchscreen sign, with a touch-to-notify button.

**Stack:** ESP32 + ILI9341 + XPT2046, Mosquitto MQTT, Python bridge (ntfy + Power Automate + Radicale), Radicale (CalDAV), and optional Teams/Flow notifications.

## Configuration (required before first build)

Wi-Fi credentials and the MQTT broker address are **not** in the sketch. Copy the template
and fill it in — the real file is gitignored:

```
cp ESP32/OfficeSign_Final/secrets.example.h ESP32/OfficeSign_Final/secrets.h
```

`secrets.h` defines `WIFI_SSID`, `WIFI_PASS` and `MQTT_HOST`. The sketch will not compile
without it (`secrets.h: No such file or directory`) — that is deliberate, not a fault.
Never replace these with inline literals.

## Repo Structure
```
office-sign/
├── README.md
├── ESP32/
│   ├── OfficeSign_Final/
│   │   ├── OfficeSign_Final.ino
│   │   └── secrets.example.h   → copy to secrets.h (gitignored)
│   ├── User_Setup.h
│   └── docs.md
├── NAS/
│   ├── docker-compose.bridge.yml
│   ├── docker-compose.mosquitto.yml
│   ├── docker-compose.radicale.yml
│   ├── Dockerfile.bridge
│   ├── bridge.py
│   └── README.md
└── PowerAutomate/
    ├── README.md
    ├── sample_compose_state.json
    └── vevent_template.ics
```

## Hardware Wiring
| Function | Pin | Description |
|-----------|-----|-------------|
| TFT_CS | 15 | Chip select |
| TFT_DC | 2 | Data/command |
| TFT_RST | -1 | Not used |
| TFT_BL | 21 | Backlight (active high) |
| HSPI SCLK | 14 | TFT clock |
| HSPI MISO | 12 | TFT MISO |
| HSPI MOSI | 13 | TFT MOSI |
| TOUCH_CS | 27 | Touch CS |
| TOUCH_SCLK | 25 | Touch clock |
| TOUCH_MISO | 39 | Touch MISO |
| TOUCH_MOSI | 32 | Touch MOSI |

See `ESP32/docs.md` and `NAS/README.md` for detailed setup.
