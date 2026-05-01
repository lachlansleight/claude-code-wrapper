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

// When true, ignore all saved provisioning values in NVS and always use the
// hardcoded WIFI_* / BRIDGE_* values above.
#define FORCE_HARDCODED_PROVISIONING false

// ---- Debug -----------------------------------------------------------------
// When true, every WebSocket frame is dumped to Serial verbatim.
// When false, only decoded event summaries are logged.
#define DEBUG_WS_VERBOSE  true

// ---- Display ---------------------------------------------------------------
// 1.28" 240x240 round IPS, GC9A01 driver, SPI on ESP32-S3.
//
// IMPORTANT: TFT_eSPI reads its pin map from `User_Setup.h` at compile time,
// not from this file. The values below are documentation only — if you
// change them here you must also change them in `robot_v2/User_Setup.h`
// (which you copy into the TFT_eSPI library folder). See the header of that
// file for instructions.
#define TFT_PIN_SCK     7
#define TFT_PIN_MOSI    9
#define TFT_PIN_DC      4
#define TFT_PIN_CS      2
#define TFT_PIN_RST     1
// Some GC9A01 breakouts expose a BL pin for PWM backlight control; others
// hardwire it to VCC. Define TFT_BL in User_Setup.h if you have one.

// ---- Motion ----------------------------------------------------------------
// Primary servo (SG92R or similar hobby servo). Must be a pin that supports
// ESP32 LEDC PWM — "D5" on most ESP32 dev boards maps to GPIO5.
#define SERVO_PIN      5
