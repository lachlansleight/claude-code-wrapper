// Copy this file to `config.h` and fill in real values.
// `config.h` is gitignored so secrets stay local.

#pragma once

// ---- WiFi ------------------------------------------------------------------
#define WIFI_SSID      "REPLACE_WITH_YOUR_SSID"
#define WIFI_PASSWORD  "REPLACE_WITH_YOUR_PASSWORD"

// ---- Bridge ----------------------------------------------------------------
// Host LAN IP of the machine running the bridge (not 127.0.0.1 — the ESP32
// needs to reach it over WiFi). Make sure the bridge was started with
// BRIDGE_HOST=0.0.0.0 so it accepts LAN connections.
#define BRIDGE_HOST    "192.168.1.10"
#define BRIDGE_PORT    8787
#define BRIDGE_TOKEN   "REPLACE_WITH_BRIDGE_TOKEN"

// ---- Debug -----------------------------------------------------------------
// When true, every WebSocket frame is dumped to Serial verbatim.
// When false, only decoded event summaries are logged.
#define DEBUG_WS_VERBOSE  true

// ---- OLED ------------------------------------------------------------------
// 128x32 SSD1306 on the default ESP32 I2C bus (SDA=21, SCL=22).
#define OLED_I2C_ADDR  0x3C
#define OLED_WIDTH     128
#define OLED_HEIGHT    32
