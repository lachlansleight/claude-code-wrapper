#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../hal/Settings.h"

namespace BridgeControl {

enum class DisplayMode : uint8_t { Face = 0, Text };

using PaletteChangeHandler = void (*)(Settings::NamedColor color, uint8_t r, uint8_t g, uint8_t b);
using DisplayModeHandler = void (*)(DisplayMode mode);
using ServoOverrideHandler = void (*)(int8_t angle, uint32_t durationMs);

void onPaletteChange(PaletteChangeHandler handler);
void onDisplayModeChange(DisplayModeHandler handler);
void onServoOverride(ServoOverrideHandler handler);

void dispatch(JsonDocument& doc);

}  // namespace BridgeControl
