#pragma once

#include <Arduino.h>

/**
 * @file Settings.h
 * @brief NVS-backed runtime settings: face palette and face/text mode.
 *
 * Holds user-tweakable presentation settings persisted to NVS in the
 * `settings_v3` namespace. The store contains:
 *  - a per-NamedColor RGB888 palette (one slot per visual concept —
 *    foreground/background plus one per verb and emotion);
 *  - a single `face_mode` boolean (true = procedural face, false =
 *    minimal text mode);
 *  - a `motors_disabled` boolean (true = servo held centred);
 *  - a schema version key — on mismatch the entire namespace is reset
 *    to the compiled-in defaults.
 *
 * Mutations are immediately persisted to NVS and bump
 * settingsVersion(). Renderers that cache derived state poll this
 * counter to know when to invalidate. `BridgeControl` is the main
 * writer — palette edits and the face/text toggle arrive over WS.
 */
namespace Settings {

/**
 * Symbolic palette slots. Order is part of the persistence schema —
 * appending new entries is safe but reordering or removing requires
 * bumping the schema version.
 */
enum class NamedColor : uint8_t {
  Background = 0,  ///< Cleared screen / face background.
  Foreground,      ///< Default text and structural lines.
  Thinking,        ///< Verb: thinking.
  Reading,         ///< Verb: reading.
  Writing,         ///< Verb: writing/editing.
  Executing,       ///< Verb: shell/exec.
  Straining,       ///< Verb: long-running / heavy work.
  Sad,             ///< Emotion: sad / negative valence.
  Joyful,          ///< Emotion: joyful (high arousal + positive).
  Excited,         ///< Emotion: excited.
  Happy,           ///< Emotion: happy (steady-state positive).
  Sleeping,        ///< Idle / sleeping.
  Attention,       ///< Wants-attention (permission requested).
  Count            ///< Sentinel; not a valid color.
};

/// Plain RGB888 triple. Storage format is RGB888 → packed as 0x00RRGGBB.
struct Rgb888 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

/**
 * Open the NVS namespace, validate the schema version (resetting to
 * defaults on mismatch) and load the palette + face_mode flag into RAM.
 * Must be called once in `setup()` before any Settings reader runs.
 */
void begin();

/// True if procedural face rendering is enabled; false = text mode.
bool faceModeEnabled();

/**
 * Toggle face / text mode. No-op if the value is unchanged. Otherwise
 * persists to NVS and bumps settingsVersion() so dependent caches
 * invalidate.
 */
void setFaceModeEnabled(bool enabled);

/// True if servo motion is disabled.
bool motorsDisabled();

/**
 * Toggle servo movement disable state. No-op if unchanged; otherwise persists
 * to NVS and bumps settingsVersion().
 */
void setMotorsDisabled(bool disabled);

/**
 * Monotonic counter that increments on every successful palette change
 * or face-mode toggle. Renderers compare against the last-seen value
 * to know when to rebuild palette-derived caches.
 */
uint32_t settingsVersion();

/// Current RGB888 for @p color.
Rgb888 colorRgb(NamedColor color);

/// Current color packed to TFT_eSPI's native RGB565.
uint16_t color565(NamedColor color);

/**
 * RGB565 of @p color with each channel multiplied by @p scale255 / 255.
 * Used to fade palette entries (e.g. dim activity dots).
 */
uint16_t color565Scaled(NamedColor color, uint8_t scale255);

/**
 * Update @p color in RAM and persist. No-op (and no version bump) if
 * the value is unchanged.
 */
void setColorRgb(NamedColor color, uint8_t r, uint8_t g, uint8_t b);

}  // namespace Settings
