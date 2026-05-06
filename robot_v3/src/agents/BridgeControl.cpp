#include "BridgeControl.h"

#include "../core/DebugLog.h"

namespace BridgeControl {

namespace {
PaletteChangeHandler sPaletteHandler = nullptr;
DisplayModeHandler sModeHandler = nullptr;
MotorsDisabledHandler sMotorsDisabledHandler = nullptr;
ServoOverrideHandler sServoHandler = nullptr;

bool tryParseNamedColor(const char* key, Settings::NamedColor* out) {
  if (!key || !out) return false;
  if (strcmp(key, "background") == 0) {
    *out = Settings::NamedColor::Background;
    return true;
  }
  if (strcmp(key, "foreground") == 0) {
    *out = Settings::NamedColor::Foreground;
    return true;
  }
  if (strcmp(key, "thinking") == 0) {
    *out = Settings::NamedColor::Thinking;
    return true;
  }
  if (strcmp(key, "reading") == 0) {
    *out = Settings::NamedColor::Reading;
    return true;
  }
  if (strcmp(key, "writing") == 0) {
    *out = Settings::NamedColor::Writing;
    return true;
  }
  if (strcmp(key, "executing") == 0) {
    *out = Settings::NamedColor::Executing;
    return true;
  }
  if (strcmp(key, "executing_long") == 0 || strcmp(key, "straining") == 0) {
    *out = Settings::NamedColor::Straining;
    return true;
  }
  if (strcmp(key, "blocked") == 0 || strcmp(key, "sad") == 0) {
    *out = Settings::NamedColor::Sad;
    return true;
  }
  if (strcmp(key, "finished") == 0 || strcmp(key, "joyful") == 0) {
    *out = Settings::NamedColor::Joyful;
    return true;
  }
  if (strcmp(key, "excited") == 0) {
    *out = Settings::NamedColor::Excited;
    return true;
  }
  if (strcmp(key, "happy") == 0) {
    *out = Settings::NamedColor::Happy;
    return true;
  }
  if (strcmp(key, "sleeping") == 0) {
    *out = Settings::NamedColor::Sleeping;
    return true;
  }
  if (strcmp(key, "wants_at") == 0 || strcmp(key, "attention") == 0) {
    *out = Settings::NamedColor::Attention;
    return true;
  }
  return false;
}
}

void onPaletteChange(PaletteChangeHandler handler) { sPaletteHandler = handler; }
void onDisplayModeChange(DisplayModeHandler handler) { sModeHandler = handler; }
void onMotorsDisabledChange(MotorsDisabledHandler handler) { sMotorsDisabledHandler = handler; }
void onServoOverride(ServoOverrideHandler handler) { sServoHandler = handler; }

void dispatch(JsonDocument& doc) {
  const char* type = doc["type"] | "";

  if (strcmp(type, "setColor") == 0) {
    if (!sPaletteHandler) return;
    const uint8_t r = (uint8_t)(doc["r"] | 0);
    const uint8_t g = (uint8_t)(doc["g"] | 0);
    const uint8_t b = (uint8_t)(doc["b"] | 0);
    Settings::NamedColor color = Settings::NamedColor::Background;

    // Current control UI sends string keys ("thinking", "blocked", ...).
    // Keep numeric index support for older senders.
    const char* key = doc["key"] | doc["name"] | "";
    if (!tryParseNamedColor(key, &color)) {
      const int colorIndex = doc["color"] | -1;
      if (colorIndex < 0 || colorIndex >= (int)Settings::NamedColor::Count) {
        LOG_WARN("setColor ignored invalid key='%s' index=%d", key ? key : "", colorIndex);
        return;
      }
      color = (Settings::NamedColor)colorIndex;
    }
    sPaletteHandler(color, r, g, b);
    return;
  }

  if (strcmp(type, "config_change") == 0) {
    if (sMotorsDisabledHandler) {
      JsonVariantConst motorsVar = doc["motors_disabled"];
      if (motorsVar.isNull()) motorsVar = doc["config"]["motors_disabled"];
      if (!motorsVar.isNull() && motorsVar.is<bool>()) {
        sMotorsDisabledHandler(motorsVar.as<bool>());
      }
    }
    if (!sModeHandler) return;
    JsonVariantConst modeVar = doc["display_mode"];
    if (modeVar.isNull()) modeVar = doc["config"]["display_mode"];
    if (!modeVar.isNull()) {
      const char* mode = modeVar.as<const char*>();
      if (mode && !strcmp(mode, "text")) {
        sModeHandler(DisplayMode::Text);
        return;
      }
      if (mode && !strcmp(mode, "debug")) {
        sModeHandler(DisplayMode::Debug);
        return;
      }
      if (mode && !strcmp(mode, "face")) {
        sModeHandler(DisplayMode::Face);
        return;
      }
    }
    JsonVariantConst faceModeVar = doc["face_mode_enabled"];
    if (faceModeVar.isNull()) faceModeVar = doc["config"]["face_mode_enabled"];
    if (!faceModeVar.isNull()) {
      const bool faceEnabled = faceModeVar.as<bool>();
      sModeHandler(faceEnabled ? DisplayMode::Face : DisplayMode::Text);
    }
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
