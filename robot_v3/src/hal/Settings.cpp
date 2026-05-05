#include "Settings.h"

#include <Preferences.h>

#include "../core/DebugLog.h"

namespace Settings {

namespace {

static constexpr const char* kNamespace = "settings_v3";
static constexpr const char* kKeySchema = "schema";
static constexpr const char* kKeyFaceMode = "face_mode";
static constexpr const char* kKeyMotorsDisabled = "motors_dis";
static constexpr const char* kColorKeyPrefix = "c";
static constexpr uint16_t kSettingsSchemaVersion = 1;

bool g_faceModeEnabled = true;
bool g_motorsDisabled = false;
uint32_t g_settingsVersion = 1;

Rgb888 g_defaultColors[(size_t)NamedColor::Count] = {
    {0, 0, 0},      // Background
    {255, 255, 255},// Foreground
    {36, 56, 120},  // Thinking
    {78, 146, 210}, // Reading
    {104, 118, 228},// Writing
    {156, 64, 216}, // Executing
    {210, 75, 220}, // Straining
    {255, 48, 24},  // Sad
    {255, 228, 32}, // Joyful
    {40, 255, 80},  // Excited
    {104, 210, 140},// Happy
    {28, 40, 72},   // Sleeping
    {255, 200, 40}, // Attention
};

Rgb888 g_colors[(size_t)NamedColor::Count];

uint16_t rgb888To565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                    ((uint16_t)(g & 0xFC) << 3) |
                    ((uint16_t)(b & 0xF8) >> 3));
}

size_t idx(NamedColor color) {
  const size_t i = (size_t)color;
  if (i >= (size_t)NamedColor::Count) return 0;
  return i;
}

uint32_t packRgb(const Rgb888& c) {
  return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

Rgb888 unpackRgb(uint32_t packed) {
  return Rgb888{
      (uint8_t)((packed >> 16) & 0xFF),
      (uint8_t)((packed >> 8) & 0xFF),
      (uint8_t)(packed & 0xFF),
  };
}

String colorKey(size_t i) {
  String key = kColorKeyPrefix;
  key += String((unsigned)i);
  return key;
}

void resetToDefaults(Preferences& p) {
  p.clear();
  p.putUShort(kKeySchema, kSettingsSchemaVersion);
  p.putBool(kKeyFaceMode, true);
  p.putBool(kKeyMotorsDisabled, false);
  for (size_t i = 0; i < (size_t)NamedColor::Count; ++i) {
    p.putULong(colorKey(i).c_str(), packRgb(g_defaultColors[i]));
  }
}

}  // namespace

void begin() {
  Preferences p;
  p.begin(kNamespace, false);
  const uint16_t schema = p.getUShort(kKeySchema, 0);
  if (schema != kSettingsSchemaVersion) {
    LOG_WARN("settings schema mismatch (%u -> %u), resetting defaults",
             (unsigned)schema, (unsigned)kSettingsSchemaVersion);
    resetToDefaults(p);
  }

  g_faceModeEnabled = p.getBool(kKeyFaceMode, true);
  g_motorsDisabled = p.getBool(kKeyMotorsDisabled, false);
  for (size_t i = 0; i < (size_t)NamedColor::Count; ++i) {
    const uint32_t packed = p.getULong(colorKey(i).c_str(), packRgb(g_defaultColors[i]));
    g_colors[i] = unpackRgb(packed);
  }
  p.end();
}

bool faceModeEnabled() { return g_faceModeEnabled; }

void setFaceModeEnabled(bool enabled) {
  if (g_faceModeEnabled == enabled) return;
  g_faceModeEnabled = enabled;
  ++g_settingsVersion;

  Preferences p;
  p.begin(kNamespace, false);
  p.putBool(kKeyFaceMode, enabled);
  p.end();
}

bool motorsDisabled() { return g_motorsDisabled; }

void setMotorsDisabled(bool disabled) {
  if (g_motorsDisabled == disabled) return;
  g_motorsDisabled = disabled;
  ++g_settingsVersion;

  Preferences p;
  p.begin(kNamespace, false);
  p.putBool(kKeyMotorsDisabled, disabled);
  p.end();
}

uint32_t settingsVersion() { return g_settingsVersion; }

Rgb888 colorRgb(NamedColor color) { return g_colors[idx(color)]; }

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
  c = {r, g, b};
  ++g_settingsVersion;

  Preferences p;
  p.begin(kNamespace, false);
  p.putULong(colorKey(idx(color)).c_str(), packRgb(c));
  p.end();
}

}  // namespace Settings
