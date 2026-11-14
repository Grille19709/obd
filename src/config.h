// config.h - devices this panel talks to (all over Bluetooth LE)

#ifndef CONFIG_H
#define CONFIG_H

// Addresses and keys of your own devices live in secrets.h (not in git).
// Copy secrets.example.h to secrets.h and fill it in.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "src/secrets.h missing - using secrets.example.h (no devices configured)"
#include "secrets.example.h"
#endif

// ---- OBD adapter: must be a Bluetooth LE ELM327 (the ESP32-S3 has no Bluetooth Classic) ----
// The adapter is taken when its address equals OBD_MAC or its name contains one of OBD_NAMES
// (case-insensitive). Names starting with "ESP32" are ignored (the old ESP32-OBD bridge).
// The "More" screen lists the BLE devices in range, the serial log shows their addresses.
// OBD_MAC: in secrets.h
const char* OBD_NAMES[] = {"obd", "vlink", "v-link", "icar", "elm", "viecar", "veepeak"};

// ---- Victron SmartSolar: VictronConnect -> product info -> Instant Readout -> details ----
// VICTRON_MAC, VICTRON_KEY: in secrets.h

// ---- LiFePO4 battery with JBD Bluetooth BMS ("" = off) ----
// While the panel is connected, the battery's phone app cannot connect (BMS allows one link).
// AKKU_MAC: in secrets.h

// ---- Phone page (https://grille19709.github.io/obd/): panel shows up as this BLE device ----
#define PHONE_BLE 1                    // 0 = do not offer the values to the phone
const char* BLE_NAME = "ESP32-OBD";
// must match docs/index.html (the phone page)
#define SERVICE_UUID "263ce341-1fb4-4abd-b38b-3bc1ea241e76"
#define WERTE_UUID   "8038be20-f5a2-421c-83ab-91d6e985bf0d"

const uint32_t OBD_STALE_MS   = 5000;    // older OBD values count as invalid
const uint32_t SOLAR_STALE_MS = 30000;   // same for solar / battery
const uint32_t OBD_RETRY_MS   = 30000;   // pause between connection attempts to the adapter
const float    AIR_PRESSURE_KPA = 101.3; // car does not report ambient pressure (PID 33)

#endif
