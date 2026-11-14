# Kiteliner – Camper Van OBD Display

Camper van panel on a Guition JC4827W543 (ESP32-S3, 4.3" 480×272 touch display), with a phone page.

- **Car**: 8 live engine values from a Bluetooth LE ELM327 adapter
- **Home**: LiFePO4 battery (JBD BMS) and Victron SmartSolar (Instant Readout)
- **More**: connection state and BLE devices in range
- **Phone page**: https://grille19709.github.io/obd/ (Chrome on Android, Web Bluetooth).
  Connects to the panel ("ESP32-OBD") and shows the same values.

## Layout

| Path | What |
|---|---|
| `src/` | Firmware (PlatformIO, Arduino) |
| `src/config.h` | Settings |
| `src/secrets.example.h` | Template for `src/secrets.h` (device addresses, Victron key; not in git) |
| `docs/` | Phone page, served by GitHub Pages |

## Build

1. `cp src/secrets.example.h src/secrets.h` and fill in your devices
2. `pio run -t upload`

If you change the phone page, raise `CACHE` in `docs/sw.js`.
