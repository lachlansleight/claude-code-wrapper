#pragma once

#include <Arduino.h>

namespace Face {

struct ParamI16 {
  int16_t value{0};
  uint8_t strength{0};  // 0 = abstain for blend weighting; 100 = full insistence.
};

/// Canonical field order for verb keyframe overrides.
enum class FieldIndex : uint8_t {
  EyeDy = 0,
  EyeRx,
  EyeOpenAmt,
  EyeArcAmt,
  EyeThick,
  EyeWaveAmp,
  EyeWaveFreq,
  EyeWaveSpeed,
  PupilDx,
  PupilDy,
  PupilR,
  MouthDy,
  MouthRx,
  MouthOpenAmt,
  MouthArcAmt,
  MouthThick,
  MouthWaveAmp,
  MouthWaveFreq,
  MouthWaveSpeed,
  FaceRot,
  FaceY,
  RingR,
  RingG,
  RingB,
  ArmMinDeg,
  ArmMaxDeg,
  ArmPeriodMs,
  ArmIntervalMs,
  Count
};

struct FaceParams {
  ParamI16 eye_dy;
  ParamI16 eye_rx;
  ParamI16 eye_open_amt;
  ParamI16 eye_arc_amt;
  ParamI16 eye_thick;
  ParamI16 eye_wave_amp;
  ParamI16 eye_wave_freq;
  ParamI16 eye_wave_speed;
  ParamI16 pupil_dx;
  ParamI16 pupil_dy;
  ParamI16 pupil_r;
  ParamI16 mouth_dy;
  ParamI16 mouth_rx;
  ParamI16 mouth_open_amt;
  ParamI16 mouth_arc_amt;
  ParamI16 mouth_thick;
  ParamI16 mouth_wave_amp;
  ParamI16 mouth_wave_freq;
  ParamI16 mouth_wave_speed;
  ParamI16 face_rot;
  ParamI16 face_y;
  ParamI16 ring_r;
  ParamI16 ring_g;
  ParamI16 ring_b;
  ParamI16 arm_min_deg;
  ParamI16 arm_max_deg;
  ParamI16 arm_period_ms;
  ParamI16 arm_interval_ms;
};

}  // namespace Face
