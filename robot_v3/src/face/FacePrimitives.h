#pragma once

#include <Arduino.h>

namespace Face {

/**
 * Blended base-layer arm motion (offsets from centre, degrees).
 * One cycle: sine arch from min → max → min over waggle_period_s,
 * then hold at min for waggle_interval_s.
 */
struct EmotionArmMotion {
  int16_t min_offset_deg;
  int16_t max_offset_deg;
  float waggle_period_s;
  float waggle_interval_s;
};

struct ParamI16 {
  int16_t value{0};
  uint8_t strength{0};  // 0 = abstain for blend weighting; 100 = full insistence.
};

/// Canonical field order for verb keyframe overrides.
enum class FieldIndex : uint8_t {
  EyeDy = 0,
  EyeRx,
  EyeTopApex,
  EyeTopCorner,
  EyeBotApex,
  EyeBotCorner,
  EyeThick,
  EyeWaveAmp,
  EyeWaveFreq,
  EyeWaveSpeed,
  PupilDx,
  PupilDy,
  PupilR,
  MouthDy,
  MouthRx,
  MouthTopApex,
  MouthTopCorner,
  MouthBotApex,
  MouthBotCorner,
  MouthThick,
  MouthWaveAmp,
  MouthWaveFreq,
  MouthWaveSpeed,
  FaceRot,
  FaceY,
  RingR,
  RingG,
  RingB,
  Count
};

struct FaceParams {
  ParamI16 eye_dy;
  ParamI16 eye_rx;
  ParamI16 eye_top_apex;
  ParamI16 eye_top_corner;
  ParamI16 eye_bot_apex;
  ParamI16 eye_bot_corner;
  ParamI16 eye_thick;
  ParamI16 eye_wave_amp;
  ParamI16 eye_wave_freq;
  ParamI16 eye_wave_speed;
  ParamI16 pupil_dx;
  ParamI16 pupil_dy;
  ParamI16 pupil_r;
  ParamI16 mouth_dy;
  ParamI16 mouth_rx;
  ParamI16 mouth_top_apex;
  ParamI16 mouth_top_corner;
  ParamI16 mouth_bot_apex;
  ParamI16 mouth_bot_corner;
  ParamI16 mouth_thick;
  ParamI16 mouth_wave_amp;
  ParamI16 mouth_wave_freq;
  ParamI16 mouth_wave_speed;
  ParamI16 face_rot;
  ParamI16 face_y;
  ParamI16 ring_r;
  ParamI16 ring_g;
  ParamI16 ring_b;
};

}  // namespace Face
