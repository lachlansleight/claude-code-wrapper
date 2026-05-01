#pragma once

#include <Arduino.h>

namespace Settings {

enum class NamedColor : uint8_t {
  Background = 0,
  Foreground,
  Thinking,
  Reading,
  Writing,
  Executing,
  ExecutingLong,
  Blocked,
  Finished,
  Excited,
  WantsAt,
  Count
};

struct Rgb888 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// Global runtime mode: true = face mode, false = text mode.
bool faceModeEnabled();
void setFaceModeEnabled(bool enabled);
uint32_t settingsVersion();

// Named color accessors/mutators.
Rgb888 colorRgb(NamedColor color);
uint16_t color565(NamedColor color);
uint16_t color565Scaled(NamedColor color, uint8_t scale255);
void setColorRgb(NamedColor color, uint8_t r, uint8_t g, uint8_t b);

}  // namespace Settings
