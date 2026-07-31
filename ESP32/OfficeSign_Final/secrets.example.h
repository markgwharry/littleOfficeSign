// secrets.example.h
//
// Copy this file to secrets.h (same directory) and fill in real values.
// secrets.h is gitignored. Do NOT commit real credentials.

#pragma once

// ---- WiFi ----
#define WIFI_SSID  "your-ssid-here"
#define WIFI_PASS  "your-wifi-password-here"

// ---- MQTT ----
// LAN address of the broker running on the NAS (see NAS/docker-compose.mosquitto.yml).
#define MQTT_HOST  "192.168.x.x"
