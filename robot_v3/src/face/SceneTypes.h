#pragma once

// Face + text pipeline types. No bridge / behaviour / hal includes.
// Colors are passed as RGB565 or RGB888 resolved by the composition layer.

#include <Arduino.h>
#include <TFT_eSPI.h>

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

/// Top-level rendering style. Mirrors AgentEvents::RenderMode and BridgeControl::DisplayMode.
enum class RenderMode : uint8_t {
  Face = 0,   ///< Procedural face.
  Text,       ///< Text status display.
  Debug,      ///< Verbose diagnostic overlay.
};

/**
 * Effective expression chosen by the composition layer from
 * (verb, emotion, overlay). Order **matters** — many tables
 * (`MotionBehaviors::kMotion`, `FrameController::kBaseTargets`,
 * `moodColorForExpression`) are indexed by this enum and rely on the
 * exact ordering. Add new entries before `Count` and update every
 * indexed table.
 */
enum class Expression : uint8_t {
  Neutral = 0,        ///< Idle, post-turn.
  Happy,              ///< Positive emotion, low arousal.
  Excited,            ///< Positive + high arousal.
  Joyful,             ///< Top-right of valence/arousal plane.
  Sad,                ///< Negative valence.
  VerbThinking,
  VerbReading,
  VerbWriting,
  VerbExecuting,
  VerbStraining,      ///< Long-running execution.
  VerbSleeping,
  OverlayWaking,      ///< Transient: one-shot wake animation.
  OverlayAttention,   ///< Transient: attention pulse for permission requests.
  Count
};

/**
 * Per-expression face geometry target. FrameController tweens between
 * these. All fields except rotation/speed are integer pixel offsets
 * (positive = down/right, expression-relative). `ring_*` is the
 * mood-ring RGB888 baked from the expression's palette colour.
 *
 * Both eye and mouth are described as a top edge curve and a bottom
 * edge curve, each as a semicircular interpolation between an apex
 * (y at lx=0) and a corner (y at lx=±half-width). When top and bottom
 * curves are mirror images about y=0 the pair traces a perfect
 * ellipse. The whole shape can be modulated by a per-shape sinusoidal
 * wave that shifts both edges together — set `*_wave_amp` to 0 to
 * disable. All curve fields interpolate continuously, so
 * cross-expression tweens never pop.
 *
 * Eye render model: top stroke (band of `eye_thick` extending
 * **outward** above the top edge), bot stroke (same below the bottom
 * edge), hollow interior with the pupil drawn behind. Strokes never
 * overlap into each other — the corners stay sharp. Out-of-envelope
 * columns are not painted, so the pupil is naturally clipped to the
 * eye shape and does not shrink with eye height.
 *
 * Mouth render model: solid fill between top and bottom curves, with
 * a minimum band thickness of `mouth_thick` when the curves collapse
 * onto each other (closed-mouth case).
 */
struct FaceParams {
  int16_t eye_dy;            ///< Vertical offset of both eyes from baseline.
  int16_t eye_rx;            ///< Eye half-width.
  int16_t eye_top_apex;      ///< Top edge y at lx=0 (eye-local; +y = down).
  int16_t eye_top_corner;    ///< Top edge y at lx=±eye_rx.
  int16_t eye_bot_apex;      ///< Bot edge y at lx=0.
  int16_t eye_bot_corner;    ///< Bot edge y at lx=±eye_rx.
  int16_t eye_thick;         ///< Per-edge stroke thickness, drawn outward.
  int16_t eye_wave_amp;      ///< Sinusoidal vertical shift applied to both edges.
  int16_t eye_wave_freq;     ///< Cycles across the eye width.
  int16_t eye_wave_speed;    ///< Phase advance, degrees/sec (0 = static).

  int16_t pupil_dx;          ///< Pupil horizontal offset within the eye.
  int16_t pupil_dy;
  int16_t pupil_r;           ///< Pupil radius (0 = no pupil).

  int16_t mouth_dy;
  int16_t mouth_rx;          ///< Mouth half-width.
  int16_t mouth_top_apex;    ///< Top edge y at lx=0 (mouth-local; +y = down).
  int16_t mouth_top_corner;  ///< Top edge y at lx=±mouth_rx.
  int16_t mouth_bot_apex;
  int16_t mouth_bot_corner;
  int16_t mouth_thick;       ///< Min collapsed band thickness; below this the band
                             ///< widens around the midpoint so closed mouths still show.
  int16_t mouth_wave_amp;    ///< Sinusoidal shift applied to both edges (zigzag).
  int16_t mouth_wave_freq;   ///< Cycles across the mouth width.
  int16_t mouth_wave_speed;  ///< Phase advance, degrees/sec.

  int16_t face_rot;          ///< Whole-face rotation in degrees.
  int16_t face_y;            ///< Whole-face vertical bob offset.

  int16_t ring_r;            ///< Mood-ring R (RGB888).
  int16_t ring_g;
  int16_t ring_b;
};

/**
 * Per-frame derived rendering state computed by FrameController.
 * Distinct from SceneContext (which is the *snapshot of the world*) —
 * SceneRenderState carries values that only make sense for *this*
 * frame: tweened mood ring colour, fade alphas, the precomputed RGB565
 * foreground/background.
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
