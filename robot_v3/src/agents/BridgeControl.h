#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../hal/Settings.h"

/**
 * @file BridgeControl.h
 * @brief Parser for **non-semantic** bridge control frames.
 *
 * Where AgentEvents handles the agent lifecycle vocabulary, BridgeControl
 * handles "device-level" remote-control messages — palette tweaks, the
 * face/text/debug display-mode toggle, and direct servo overrides for
 * testing. These are typically sent by an operator UI rather than a
 * running agent.
 *
 * Each handler is a single-slot callback. Composition is done at the
 * sketch level: the registered handler is responsible for actually
 * applying the change (e.g. calling Settings::setColorRgb,
 * Motion::holdPosition).
 */
namespace BridgeControl {

/// Mirrors AgentEvents::RenderMode but as a strongly-typed enum class.
enum class DisplayMode : uint8_t { Face = 0, Text, Debug };

/**
 * Fired on `setColor` frames. Index is validated against
 * Settings::NamedColor::Count and out-of-range values are dropped with
 * a warning before the handler is called.
 */
using PaletteChangeHandler = void (*)(Settings::NamedColor color, uint8_t r, uint8_t g, uint8_t b);

/**
 * Fired on `config_change` frames. Recognises an explicit
 * `display_mode` string ("face"/"text"/"debug") at either the top
 * level or under `config.display_mode`, and falls back to the legacy
 * boolean `face_mode_enabled` (defaulting to true) when none is
 * present.
 */
using DisplayModeHandler = void (*)(DisplayMode mode);

/**
 * Fired on `set_servo_position` frames. `angle` is the offset from
 * centre in degrees; `duration_ms` defaults to 1000. Typically wired
 * to Motion::holdPosition().
 */
using ServoOverrideHandler = void (*)(int8_t angle, uint32_t durationMs);

/// Register a palette handler. Single slot. Pass nullptr to ignore the frame.
void onPaletteChange(PaletteChangeHandler handler);
/// Register a display-mode handler. Single slot.
void onDisplayModeChange(DisplayModeHandler handler);
/// Register a servo-override handler. Single slot.
void onServoOverride(ServoOverrideHandler handler);

/**
 * Inspect @p doc and route to the appropriate handler. Recognised
 * `type` values: `setColor`, `config_change`, `set_servo_position`.
 * Frames with other types or no registered handler are silently
 * ignored — AgentEvents::dispatch handles `agent_event` etc.
 */
void dispatch(JsonDocument& doc);

}  // namespace BridgeControl
