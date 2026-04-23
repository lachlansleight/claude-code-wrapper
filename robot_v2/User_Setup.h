// TFT_eSPI configuration for robot_v2.
//
// HOW TO USE
// ----------
// TFT_eSPI doesn't read this file from the sketch directory — it reads from
// its own library folder. To make the library use this config:
//
//   1. Find your TFT_eSPI library folder, e.g.
//        Windows:  Documents/Arduino/libraries/TFT_eSPI/
//      The folder contains its own `User_Setup.h` and a `User_Setup_Select.h`.
//   2. Either:
//        a) Replace the library's `User_Setup.h` with this file, OR
//        b) Copy this file into the library folder and edit
//           `User_Setup_Select.h` to `#include <User_Setup.h>` (it does by
//           default).
//   3. Re-compile the sketch in the Arduino IDE.
//
// If you change pins here, also update `config.h` so the documentation
// matches reality.

#define USER_SETUP_INFO "robot_v2 GC9A01 round TFT on ESP32-S3"

// ---- Driver ----------------------------------------------------------------
#define GC9A01_DRIVER

#define TFT_WIDTH   240
#define TFT_HEIGHT  240

// ---- SPI bus ---------------------------------------------------------------
// USE_HSPI_PORT selects SPI2 on the S3. Required for stable DMA on this MCU
// — without it, only the first frame renders. See GC9A01_RESEARCH.md.
#define USE_HSPI_PORT

// ---- Pins (ESP32-S3) -------------------------------------------------------
// MISO unused — display is write-only from the MCU's side.
#define TFT_MISO  -1
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_DC    13
#define TFT_CS    10
#define TFT_RST   14
// Backlight: PWM-driven from the firmware via LEDC. Wire to a normal GPIO,
// not a strapping pin.
#define TFT_BL     9
#define TFT_BACKLIGHT_ON HIGH

// ---- Fonts -----------------------------------------------------------------
// Built-in fonts compiled into TFT_eSPI. Keep the set small until we know
// which we actually use; each font costs flash.
#define LOAD_GLCD     // 6x8 default font
#define LOAD_FONT2    // 16-px sans (used for body text)
#define LOAD_FONT4    // 26-px sans (large header / status)
#define LOAD_GFXFF    // Adafruit_GFX free fonts compatibility
#define SMOOTH_FONT

// ---- SPI clock -------------------------------------------------------------
// 80 MHz works on short, well-grounded wiring; drop to 40 MHz if you see
// glitches or tearing.
#define SPI_FREQUENCY       80000000
#define SPI_READ_FREQUENCY  20000000
