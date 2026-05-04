#pragma once

// Face + text pipeline types. No bridge / behaviour / hal includes.
// Colors are passed as RGB565 or RGB888 resolved by the composition layer.

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace Face {

enum class Expression : uint8_t {
  Neutral = 0,
  Happy,
  Excited,
  Joyful,
  Sad,
  VerbThinking,
  VerbReading,
  VerbWriting,
  VerbExecuting,
  VerbStraining,
  VerbSleeping,
  OverlayWaking,
  OverlayAttention,
  Count
};

struct FaceParams {
  int16_t eye_dy;
  int16_t eye_rx;
  int16_t eye_ry;
  int16_t eye_stroke;
  int16_t eye_curve;
  int16_t pupil_dx;
  int16_t pupil_dy;
  int16_t pupil_r;
  int16_t mouth_dy;
  int16_t mouth_w;
  int16_t mouth_curve;
  int16_t mouth_open_h;
  int16_t mouth_thick;
  int16_t face_rot;
  int16_t face_y;
  int16_t ring_r;
  int16_t ring_g;
  int16_t ring_b;
};

struct SceneRenderState {
  Expression expression;
  float mood_r;
  float mood_g;
  float mood_b;
  float read_stream_alpha;
  float write_stream_alpha;
  uint32_t progress_fade_start_ms;
  uint16_t fade_read_count;
  uint16_t fade_write_count;
  uint16_t fg565;
  uint16_t bg565;
  uint16_t divider565;
};

struct SceneContext {
  Expression effective_expression;
  uint32_t expression_entered_at_ms;

  float mood_v;
  float mood_a;

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
};

static constexpr int16_t kCx = 120;
static constexpr int16_t kCy = 120;
static constexpr int16_t kEyeY = 95;
static constexpr int16_t kEyeLX = 85;
static constexpr int16_t kEyeRX = 155;
static constexpr int16_t kMouthY = 165;
static constexpr int16_t kPivotY = 130;

inline float clamp01(float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }

inline float smoothstep01(float t) {
  t = clamp01(t);
  return t * t * (3 - 2 * t);
}

inline uint16_t rgb888To565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) |
                    ((uint16_t)(b & 0xF8) >> 3));
}

const char* expressionName(Expression e);

}  // namespace Face
