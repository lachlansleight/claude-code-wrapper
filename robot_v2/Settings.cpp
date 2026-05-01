#include "Settings.h"

namespace Settings {

namespace {

static bool g_faceModeEnabled = true;
static uint32_t g_settingsVersion = 1;

static Rgb888 g_colors[(size_t)NamedColor::Count] = {
    /* Background    */ {0, 0, 0},
    /* Foreground    */ {255, 255, 255},
    /* Thinking      */ {36, 56, 120},
    /* Reading       */ {78, 146, 210},
    /* Writing       */ {104, 118, 228},
    /* Executing     */ {156, 64, 216},
    /* ExecutingLong */ {210, 75, 220},
    /* Blocked       */ {255, 48, 24},
    /* Finished      */ {255, 228, 32},
    /* Excited       */ {40, 255, 80},
    /* WantsAt       */ {255, 200, 40},
};

static uint16_t rgb888To565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                    ((uint16_t)(g & 0xFC) << 3) |
                    ((uint16_t)(b & 0xF8) >> 3));
}

static size_t idx(NamedColor color) {
  const size_t i = (size_t)color;
  if (i >= (size_t)NamedColor::Count) return 0;
  return i;
}

}  // namespace

bool faceModeEnabled() { return g_faceModeEnabled; }

void setFaceModeEnabled(bool enabled) {
  if (g_faceModeEnabled == enabled) return;
  g_faceModeEnabled = enabled;
  ++g_settingsVersion;
}

uint32_t settingsVersion() { return g_settingsVersion; }

Rgb888 colorRgb(NamedColor color) {
  return g_colors[idx(color)];
}

uint16_t color565(NamedColor color) {
  const Rgb888 c = colorRgb(color);
  return rgb888To565(c.r, c.g, c.b);
}

uint16_t color565Scaled(NamedColor color, uint8_t scale255) {
  const Rgb888 c = colorRgb(color);
  const uint8_t r = (uint8_t)(((uint16_t)c.r * scale255) / 255u);
  const uint8_t g = (uint8_t)(((uint16_t)c.g * scale255) / 255u);
  const uint8_t b = (uint8_t)(((uint16_t)c.b * scale255) / 255u);
  return rgb888To565(r, g, b);
}

void setColorRgb(NamedColor color, uint8_t r, uint8_t g, uint8_t b) {
  Rgb888& c = g_colors[idx(color)];
  if (c.r == r && c.g == g && c.b == b) return;
  c.r = r;
  c.g = g;
  c.b = b;
  ++g_settingsVersion;
}

}  // namespace Settings
