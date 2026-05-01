#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Personality.h"
#include "Settings.h"

namespace Face {

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
  Personality::State state;
  float mood_r;
  float mood_g;
  float mood_b;
  float read_stream_alpha;
  float write_stream_alpha;
  uint32_t progress_fade_start_ms;
  uint16_t fade_read_count;
  uint16_t fade_write_count;
};

static constexpr int16_t kCx = 120;
static constexpr int16_t kCy = 120;
static constexpr int16_t kEyeY = 95;
static constexpr int16_t kEyeLX = 85;
static constexpr int16_t kEyeRX = 155;
static constexpr int16_t kMouthY = 165;
static constexpr int16_t kPivotY = 130;
static inline uint16_t kFg() { return Settings::color565(Settings::NamedColor::Foreground); }
static inline uint16_t kBg() { return Settings::color565(Settings::NamedColor::Background); }

static inline float clamp01(float t) {
  return t < 0 ? 0 : (t > 1 ? 1 : t);
}

static inline float smoothstep01(float t) {
  t = clamp01(t);
  return t * t * (3 - 2 * t);
}

static inline uint16_t rgb888To565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                    ((uint16_t)(g & 0xFC) << 3) |
                    ((uint16_t)(b & 0xF8) >> 3));
}

}  // namespace Face
