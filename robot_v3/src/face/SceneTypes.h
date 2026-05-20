#pragma once

// Face + text pipeline types. No bridge / behaviour / hal includes.
// Colors are passed as RGB565 or RGB888 resolved by the composition layer.

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "FacePrimitives.h"
#include "FaceEnums.h"

/**
 * @file SceneTypes.h
 * @brief Pure data types and helpers shared by every face/text renderer.
 *
 * This header is the contract between the composition layer
 * (`SceneContextFill`, `FrameController`) and the rendering modules
 * (`FaceRenderer`, `TextScene`, `MoodRingRenderer`, etc). It contains
 * **no behaviour** — just enums, structs and a few inline math
 * primitives. By design, none of the bridge, HAL, or behaviour layers
 * are pulled in: scene types must compile in isolation.
 *
 * Colour values move through this layer in two flavours:
 *  - **RGB565** (`uint16_t`) for anything that hits the TFT directly,
 *  - **RGB888** triples (`uint8_t r,g,b`) for palette / mood-ring
 *    inputs that get blended in floating point first.
 *
 * The 240×240 round display has a fixed visual layout described by the
 * `kCx`, `kCy`, `kEyeY`, etc. anchor constants. Renderers should treat
 * those as authoritative — the rotation pivot for face tilt is at
 * `(kCx, kPivotY)`.
 */
namespace Face {

// `Expression` and `isEmotionExpression` live in FaceEnums.h (included above).

/// Top-level rendering style. Mirrors AgentEvents::RenderMode and BridgeControl::DisplayMode.
enum class RenderMode : uint8_t {
  Face = 0,   ///< Procedural face.
  Text,       ///< Text status display.
  Debug,      ///< Verbose diagnostic overlay.
};

// FaceParams / FieldIndex / ParamI16 / EmotionArmMotion live in FacePrimitives.h.

const ParamI16& fieldConstRef(const FaceParams& p, FieldIndex i);
ParamI16& fieldMutRef(FaceParams& p, FieldIndex i);

/** Per-field combine of emotion layer `e` with optional verb override (`06` §4, `08`). */
ParamI16 combineEmotionVerbField(const ParamI16& e, bool hasVerb, const ParamI16& v);

FaceParams combineEmotionVerbFace(const FaceParams& emotion, const bool* verbHas,
                                  const ParamI16* verbVals);

/** Lerp each `.value` toward `target`; copy each `.strength` from `target`. */
void smoothFaceValuesToward(FaceParams& state, const FaceParams& target, float alpha);

/**
 * Per-frame derived rendering state computed by FrameController.
 * Distinct from SceneContext (which is the *snapshot of the world*) —
 * SceneRenderState carries values that only make sense for *this*
 * frame: smoothed mood ring colour (from FaceParams ring_*), fade alphas,
 * the precomputed RGB565 foreground/background.
 */
struct SceneRenderState {
  Expression expression;             ///< Effective expression to render.
  float mood_r;                      ///< Tweened mood-ring RGB888 (float for smoothing).
  float mood_g;
  float mood_b;
  float read_stream_alpha;           ///< 0..1 alpha for the read-effect token stream.
  float write_stream_alpha;
  uint32_t progress_fade_start_ms;   ///< Non-zero while the activity-dot fade-out is running.
  uint16_t fade_read_count;          ///< Frozen counts captured at the start of the fade.
  uint16_t fade_write_count;
  uint16_t fg565;                    ///< Resolved RGB565 foreground.
  uint16_t bg565;                    ///< Resolved RGB565 background.
  uint16_t divider565;               ///< Hairline colour for the text/face divider.
  float eye_wave_phase_rad;          ///< Pre-integrated phase for eye wave modulation.
  float mouth_wave_phase_rad;        ///< Pre-integrated phase for mouth wave modulation.
};

/**
 * Scene-input snapshot built once per frame by SceneContextFill from
 * AgentEvents + VerbSystem + EmotionSystem + Settings. **Read-only**
 * inside renderers; do not mutate. String fields are fixed-length
 * char buffers sized for typical agent payloads with truncation as
 * the fallback.
 *
 * Field groups:
 *  - **Behaviour**: effective_expression + entry timestamp + raw mood.
 *  - **Identity**: latched_session, pending_permission.
 *  - **Text strings**: status_line / subtitle_tool / body_text +
 *    timing fields used by TextScene.
 *  - **Per-turn counters**: read/write_tools_this_turn drive the
 *    activity-dot rings.
 *  - **Connection / mode**: ws_connected, face_mode, render_mode,
 *    settings_version (used by FrameController to bust caches).
 *  - **Palette**: accent / fg / bg as RGB888 triples.
 *  - **Diagnostic**: snapped_emotion + verb_* fields populated for
 *    the debug renderer.
 *  - **Held drivers**: parallel arrays of (id, target) for the
 *    EmotionSystem held-driver display.
 */
struct SceneContext {
  Expression effective_expression;
  uint32_t expression_entered_at_ms;

  float mood_v;          ///< Raw valence in [-1, +1].
  float mood_a;          ///< Raw activation in [0, 1].

  /**
   * Emotion-layer output (ParamI16 per field): continuous blend at (mood_v,
   * mood_a). FrameController smooths these values, combines with verb
   * timelines when active, then applies idle/breath on top. Verbs/overlays do
   * not replace this row — verb face comes from `VerbTimeline` sampling +
   * combine.
   */
  FaceParams base_face_params;

  char latched_session[40];
  char pending_permission[48];
  char status_line[80];

  char body_text[512];
  char subtitle_tool[320];
  uint32_t thinking_title_since_ms;
  char latest_shell_command[160];
  char latest_read_target[160];
  char latest_write_target[160];
  uint32_t turn_started_wall_ms;
  uint32_t done_turn_elapsed_ms;

  uint16_t read_tools_this_turn;
  uint16_t write_tools_this_turn;

  bool ws_connected;
  bool face_mode;
  uint8_t render_mode;        ///< RenderMode value, kept as uint8_t for ABI stability.
  uint32_t settings_version;

  uint8_t accent_r;
  uint8_t accent_g;
  uint8_t accent_b;
  uint8_t fg_r;
  uint8_t fg_g;
  uint8_t fg_b;
  uint8_t bg_r;
  uint8_t bg_g;
  uint8_t bg_b;

  char snapped_emotion[16];
  char pending_snapped_emotion[16];
  bool pending_snap_active;
  uint32_t pending_snap_since_ms;

  char verb_current[24];
  char verb_effective[24];
  bool verb_overlay_active;
  bool verb_overlay_queued;
  uint32_t verb_time_in_current_ms;
  uint32_t verb_linger_remaining_ms;
  uint32_t verb_overlay_remaining_ms;

  uint8_t held_driver_count;
  uint8_t held_driver_ids[8];
  float held_driver_targets[8];
};

/// Screen centre X (240/2).
static constexpr int16_t kCx = 120;
/// Screen centre Y (240/2).
static constexpr int16_t kCy = 120;
/// Eye baseline Y (above centre).
static constexpr int16_t kEyeY = 95;
/// Left eye centre X.
static constexpr int16_t kEyeLX = 85;
/// Right eye centre X.
static constexpr int16_t kEyeRX = 155;
/// Mouth baseline Y (below centre).
static constexpr int16_t kMouthY = 165;
/// Pivot Y for whole-face rotation. Offset from kCy so tilts feel anchored at the chin.
static constexpr int16_t kPivotY = 130;

/// Clamp @p t to [0, 1].
inline float clamp01(float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }

/// Smoothstep: 3t² − 2t³ on [0, 1]. Used everywhere we tween geometry.
inline float smoothstep01(float t) {
  t = clamp01(t);
  return t * t * (3 - 2 * t);
}

/// Pack an RGB888 triple into TFT_eSPI's native RGB565.
inline uint16_t rgb888To565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) |
                    ((uint16_t)(b & 0xF8) >> 3));
}

/// Stable lowercase string for @p e ("verb_thinking", "joyful", ...). "?" if unknown.
const char* expressionName(Expression e);

}  // namespace Face
