#include "BridgeControl.h"

#include "../core/DebugLog.h"

namespace BridgeControl {

namespace {
PaletteChangeHandler sPaletteHandler = nullptr;
DisplayModeHandler sModeHandler = nullptr;
ServoOverrideHandler sServoHandler = nullptr;
}

void onPaletteChange(PaletteChangeHandler handler) { sPaletteHandler = handler; }
void onDisplayModeChange(DisplayModeHandler handler) { sModeHandler = handler; }
void onServoOverride(ServoOverrideHandler handler) { sServoHandler = handler; }

void dispatch(JsonDocument& doc) {
  const char* type = doc["type"] | "";

  if (strcmp(type, "setColor") == 0) {
    if (!sPaletteHandler) return;
    const int color = doc["color"] | 0;
    const uint8_t r = (uint8_t)(doc["r"] | 0);
    const uint8_t g = (uint8_t)(doc["g"] | 0);
    const uint8_t b = (uint8_t)(doc["b"] | 0);
    if (color < 0 || color >= (int)Settings::NamedColor::Count) {
      LOG_WARN("setColor ignored invalid color index=%d", color);
      return;
    }
    sPaletteHandler((Settings::NamedColor)color, r, g, b);
    return;
  }

  if (strcmp(type, "config_change") == 0) {
    if (!sModeHandler) return;
    JsonVariantConst modeVar = doc["display_mode"];
    if (modeVar.isNull()) modeVar = doc["config"]["display_mode"];
    if (!modeVar.isNull()) {
      const char* mode = modeVar.as<const char*>();
      if (mode && !strcmp(mode, "text")) {
        sModeHandler(DisplayMode::Text);
        return;
      }
      if (mode && !strcmp(mode, "face")) {
        sModeHandler(DisplayMode::Face);
        return;
      }
    }
    const bool faceEnabled = doc["face_mode_enabled"] | true;
    sModeHandler(faceEnabled ? DisplayMode::Face : DisplayMode::Text);
    return;
  }

  if (strcmp(type, "set_servo_position") == 0) {
    if (!sServoHandler) return;
    const int8_t angle = (int8_t)(doc["angle"] | 0);
    const uint32_t durationMs = (uint32_t)(doc["duration_ms"] | 1000);
    sServoHandler(angle, durationMs);
    return;
  }
}

}  // namespace BridgeControl
