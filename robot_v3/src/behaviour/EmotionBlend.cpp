#include "EmotionBlend.h"

#include <math.h>

#include "../face/FACE_CONFIG.h"
#include "../face/FrameController.h"
#include "EmotionSystem.h"
#include "EmotionTriangulation.h"

namespace EmotionBlend {

namespace {

constexpr float kBaryEps = 1e-5f;

float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

// Compute barycentric weights of (v, a) inside the triangle formed by
// anchors at indices i0/i1/i2. Returns false if the triangle is
// degenerate (zero area) — caller skips it.
bool barycentric(float v, float a, uint16_t i0, uint16_t i1, uint16_t i2,
                 float& l0, float& l1, float& l2) {
  const EmotionSystem::Anchor& A = EmotionSystem::kAnchors[i0];
  const EmotionSystem::Anchor& B = EmotionSystem::kAnchors[i1];
  const EmotionSystem::Anchor& C = EmotionSystem::kAnchors[i2];

  const float denom = (B.a - C.a) * (A.v - C.v) + (C.v - B.v) * (A.a - C.a);
  if (fabsf(denom) < 1e-12f) return false;

  l0 = ((B.a - C.a) * (v - C.v) + (C.v - B.v) * (a - C.a)) / denom;
  l1 = ((C.a - A.a) * (v - C.v) + (A.v - C.v) * (a - C.a)) / denom;
  l2 = 1.0f - l0 - l1;
  return true;
}

bool findTriangle(float v, float a, uint16_t& outI0, uint16_t& outI1,
                  uint16_t& outI2, float& outL0, float& outL1, float& outL2) {
  for (size_t t = 0; t < EmotionSystem::kTriangleCount; ++t) {
    const EmotionSystem::Triangle& tri = EmotionSystem::kTriangles[t];
    float l0, l1, l2;
    if (!barycentric(v, a, tri.i0, tri.i1, tri.i2, l0, l1, l2)) continue;
    if (l0 >= -kBaryEps && l1 >= -kBaryEps && l2 >= -kBaryEps) {
      // Clamp negatives (edge tolerance) and renormalise to keep weights summing to 1.
      if (l0 < 0.0f) l0 = 0.0f;
      if (l1 < 0.0f) l1 = 0.0f;
      if (l2 < 0.0f) l2 = 0.0f;
      const float s = l0 + l1 + l2;
      if (s > 1e-12f) {
        const float inv = 1.0f / s;
        l0 *= inv;
        l1 *= inv;
        l2 *= inv;
      }
      outI0 = tri.i0;
      outI1 = tri.i1;
      outI2 = tri.i2;
      outL0 = l0;
      outL1 = l1;
      outL2 = l2;
      return true;
    }
  }
  return false;
}

Face::ParamI16 blendParam3(const Face::ParamI16& a, const Face::ParamI16& b, const Face::ParamI16& c,
                           float la, float lb, float lc) {
  const float wa = la * (float)a.strength;
  const float wb = lb * (float)b.strength;
  const float wc = lc * (float)c.strength;
  const float W = wa + wb + wc;
  Face::ParamI16 out{};
  if (W > 1e-6f) {
    out.value = (int16_t)lroundf((wa * (float)a.value + wb * (float)b.value + wc * (float)c.value) / W);
    float st = la * (float)a.strength + lb * (float)b.strength + lc * (float)c.strength;
    out.strength = (uint8_t)lroundf(st);
    if (out.strength > 100) out.strength = 100;
  } else {
    out.value = (int16_t)lroundf((float)a.value * la + (float)b.value * lb + (float)c.value * lc);
    out.strength = 0;
  }
  return out;
}

static float blendFloat(float a, float b, float c, float la, float lb, float lc) {
  return a * la + b * lb + c * lc;
}

Face::FaceParams blendThree(const Face::FaceParams& A, const Face::FaceParams& B,
                            const Face::FaceParams& C, float la, float lb, float lc) {
  Face::FaceParams r;
  r.eye_dy = blendParam3(A.eye_dy, B.eye_dy, C.eye_dy, la, lb, lc);
  r.eye_rx = blendParam3(A.eye_rx, B.eye_rx, C.eye_rx, la, lb, lc);
  r.eye_open_amt = blendParam3(A.eye_open_amt, B.eye_open_amt, C.eye_open_amt, la, lb, lc);
  r.eye_arc_amt = blendParam3(A.eye_arc_amt, B.eye_arc_amt, C.eye_arc_amt, la, lb, lc);
  r.eye_thick = blendParam3(A.eye_thick, B.eye_thick, C.eye_thick, la, lb, lc);
  r.eye_wave_amp = blendParam3(A.eye_wave_amp, B.eye_wave_amp, C.eye_wave_amp, la, lb, lc);
  r.eye_wave_freq = blendParam3(A.eye_wave_freq, B.eye_wave_freq, C.eye_wave_freq, la, lb, lc);
  r.eye_wave_speed = blendParam3(A.eye_wave_speed, B.eye_wave_speed, C.eye_wave_speed, la, lb, lc);
  r.pupil_dx = blendParam3(A.pupil_dx, B.pupil_dx, C.pupil_dx, la, lb, lc);
  r.pupil_dy = blendParam3(A.pupil_dy, B.pupil_dy, C.pupil_dy, la, lb, lc);
  r.pupil_r = blendParam3(A.pupil_r, B.pupil_r, C.pupil_r, la, lb, lc);
  r.mouth_dy = blendParam3(A.mouth_dy, B.mouth_dy, C.mouth_dy, la, lb, lc);
  r.mouth_rx = blendParam3(A.mouth_rx, B.mouth_rx, C.mouth_rx, la, lb, lc);
  r.mouth_open_amt = blendParam3(A.mouth_open_amt, B.mouth_open_amt, C.mouth_open_amt, la, lb, lc);
  r.mouth_arc_amt = blendParam3(A.mouth_arc_amt, B.mouth_arc_amt, C.mouth_arc_amt, la, lb, lc);
  r.mouth_thick = blendParam3(A.mouth_thick, B.mouth_thick, C.mouth_thick, la, lb, lc);
  r.mouth_wave_amp = blendParam3(A.mouth_wave_amp, B.mouth_wave_amp, C.mouth_wave_amp, la, lb, lc);
  r.mouth_wave_freq =
      blendParam3(A.mouth_wave_freq, B.mouth_wave_freq, C.mouth_wave_freq, la, lb, lc);
  r.mouth_wave_speed =
      blendParam3(A.mouth_wave_speed, B.mouth_wave_speed, C.mouth_wave_speed, la, lb, lc);
  r.face_rot = blendParam3(A.face_rot, B.face_rot, C.face_rot, la, lb, lc);
  r.face_y = blendParam3(A.face_y, B.face_y, C.face_y, la, lb, lc);
  r.ring_r = blendParam3(A.ring_r, B.ring_r, C.ring_r, la, lb, lc);
  r.ring_g = blendParam3(A.ring_g, B.ring_g, C.ring_g, la, lb, lc);
  r.ring_b = blendParam3(A.ring_b, B.ring_b, C.ring_b, la, lb, lc);
  r.arm_min_deg = blendParam3(A.arm_min_deg, B.arm_min_deg, C.arm_min_deg, la, lb, lc);
  r.arm_max_deg = blendParam3(A.arm_max_deg, B.arm_max_deg, C.arm_max_deg, la, lb, lc);
  r.arm_period_ms = blendParam3(A.arm_period_ms, B.arm_period_ms, C.arm_period_ms, la, lb, lc);
  r.arm_interval_ms = blendParam3(A.arm_interval_ms, B.arm_interval_ms, C.arm_interval_ms, la, lb, lc);
  return r;
}

static FaceConfig::GazeStyle winningGazeStyle(FaceConfig::GazeStyle ga, FaceConfig::GazeStyle gb,
                                              FaceConfig::GazeStyle gc, float la, float lb,
                                              float lc) {
  if (la >= lb && la >= lc) return ga;
  if (lb >= lc) return gb;
  return gc;
}

static FaceConfig::IdleAnimRow blendIdleThree(const FaceConfig::IdleAnimRow& A,
                                              const FaceConfig::IdleAnimRow& B,
                                              const FaceConfig::IdleAnimRow& C, float la,
                                              float lb, float lc) {
  FaceConfig::IdleAnimRow r{};
  r.blink_period_min_ms = (uint16_t)lroundf(blendFloat((float)A.blink_period_min_ms,
                                                       (float)B.blink_period_min_ms,
                                                       (float)C.blink_period_min_ms, la, lb, lc));
  r.blink_period_max_ms = (uint16_t)lroundf(blendFloat((float)A.blink_period_max_ms,
                                                       (float)B.blink_period_max_ms,
                                                       (float)C.blink_period_max_ms, la, lb, lc));
  r.blink_close_ms = (uint16_t)lroundf(
      blendFloat((float)A.blink_close_ms, (float)B.blink_close_ms, (float)C.blink_close_ms, la, lb,
                 lc));
  r.blink_open_ms = (uint16_t)lroundf(
      blendFloat((float)A.blink_open_ms, (float)B.blink_open_ms, (float)C.blink_open_ms, la, lb,
                 lc));
  const bool allBobHeur = (A.bob_amplitude_px == FaceConfig::kBobAmpFollowEmotionArm &&
                           B.bob_amplitude_px == FaceConfig::kBobAmpFollowEmotionArm &&
                           C.bob_amplitude_px == FaceConfig::kBobAmpFollowEmotionArm);
  if (allBobHeur) {
    r.bob_amplitude_px = FaceConfig::kBobAmpFollowEmotionArm;
  } else {
    auto nz = [](int16_t x) -> float {
      return (x == FaceConfig::kBobAmpFollowEmotionArm) ? 0.0f : (float)x;
    };
    r.bob_amplitude_px =
        (int16_t)lroundf(blendFloat(nz(A.bob_amplitude_px), nz(B.bob_amplitude_px),
                                    nz(C.bob_amplitude_px), la, lb, lc));
  }
  r.gaze_style = winningGazeStyle(A.gaze_style, B.gaze_style, C.gaze_style, la, lb, lc);
  r.gaze_move_ms = (uint16_t)lroundf(
      blendFloat((float)A.gaze_move_ms, (float)B.gaze_move_ms, (float)C.gaze_move_ms, la, lb, lc));
  r.gaze_rand_span_x =
      (int16_t)lroundf(blendFloat((float)A.gaze_rand_span_x, (float)B.gaze_rand_span_x,
                                  (float)C.gaze_rand_span_x, la, lb, lc));
  r.gaze_rand_span_y =
      (int16_t)lroundf(blendFloat((float)A.gaze_rand_span_y, (float)B.gaze_rand_span_y,
                                  (float)C.gaze_rand_span_y, la, lb, lc));
  r.gaze_reroll_min_ms = (uint32_t)lroundf(blendFloat((float)A.gaze_reroll_min_ms,
                                                      (float)B.gaze_reroll_min_ms,
                                                      (float)C.gaze_reroll_min_ms, la, lb, lc));
  r.gaze_reroll_max_ms = (uint32_t)lroundf(blendFloat((float)A.gaze_reroll_max_ms,
                                                      (float)B.gaze_reroll_max_ms,
                                                      (float)C.gaze_reroll_max_ms, la, lb, lc));
  r.gaze_scan_period_ms = (uint32_t)lroundf(blendFloat((float)A.gaze_scan_period_ms,
                                                       (float)B.gaze_scan_period_ms,
                                                       (float)C.gaze_scan_period_ms, la, lb, lc));
  r.gaze_amp_x =
      (int16_t)lroundf(blendFloat((float)A.gaze_amp_x, (float)B.gaze_amp_x, (float)C.gaze_amp_x,
                                  la, lb, lc));
  r.gaze_amp_y =
      (int16_t)lroundf(blendFloat((float)A.gaze_amp_y, (float)B.gaze_amp_y, (float)C.gaze_amp_y,
                                  la, lb, lc));
  if (r.blink_period_max_ms < r.blink_period_min_ms)
    r.blink_period_max_ms = r.blink_period_min_ms;
  return r;
}

}  // namespace

Face::FaceParams blendedFaceParams(float v, float a) {
  v = clampf(v, -1.0f, 1.0f);
  a = clampf(a, 0.0f, 1.0f);

  uint16_t i0 = 0, i1 = 0, i2 = 0;
  float l0 = 0.0f, l1 = 0.0f, l2 = 0.0f;
  if (!findTriangle(v, a, i0, i1, i2, l0, l1, l2)) {
    // Defensive fallback: nearest anchor.
    float best = INFINITY;
    uint16_t bestIdx = 0;
    for (size_t i = 0; i < EmotionSystem::kAnchorCount; ++i) {
      const EmotionSystem::Anchor& an = EmotionSystem::kAnchors[i];
      const float dv = v - an.v;
      const float da = a - an.a;
      const float d = dv * dv + da * da;
      if (d < best) {
        best = d;
        bestIdx = (uint16_t)i;
      }
    }
    return Face::baseTargetFor(
        FaceConfig::expressionForNamedEmotion(EmotionSystem::kAnchors[bestIdx].emotion));
  }

  const Face::Expression eA =
      FaceConfig::expressionForNamedEmotion(EmotionSystem::kAnchors[i0].emotion);
  const Face::Expression eB =
      FaceConfig::expressionForNamedEmotion(EmotionSystem::kAnchors[i1].emotion);
  const Face::Expression eC =
      FaceConfig::expressionForNamedEmotion(EmotionSystem::kAnchors[i2].emotion);

  return blendThree(Face::baseTargetFor(eA), Face::baseTargetFor(eB),
                    Face::baseTargetFor(eC), l0, l1, l2);
}

FaceConfig::IdleAnimRow blendedIdleAnim(float v, float a) {
  v = clampf(v, -1.0f, 1.0f);
  a = clampf(a, 0.0f, 1.0f);

  uint16_t i0 = 0, i1 = 0, i2 = 0;
  float l0 = 0.0f, l1 = 0.0f, l2 = 0.0f;
  if (!findTriangle(v, a, i0, i1, i2, l0, l1, l2)) {
    float best = INFINITY;
    uint16_t bestIdx = 0;
    for (size_t i = 0; i < EmotionSystem::kAnchorCount; ++i) {
      const EmotionSystem::Anchor& an = EmotionSystem::kAnchors[i];
      const float dv = v - an.v;
      const float da = a - an.a;
      const float d = dv * dv + da * da;
      if (d < best) {
        best = d;
        bestIdx = (uint16_t)i;
      }
    }
    const Face::Expression ex =
        FaceConfig::expressionForNamedEmotion(EmotionSystem::kAnchors[bestIdx].emotion);
    return FaceConfig::kIdleAnim[(uint8_t)ex];
  }

  const Face::Expression eA =
      FaceConfig::expressionForNamedEmotion(EmotionSystem::kAnchors[i0].emotion);
  const Face::Expression eB =
      FaceConfig::expressionForNamedEmotion(EmotionSystem::kAnchors[i1].emotion);
  const Face::Expression eC =
      FaceConfig::expressionForNamedEmotion(EmotionSystem::kAnchors[i2].emotion);

  return blendIdleThree(FaceConfig::kIdleAnim[(uint8_t)eA], FaceConfig::kIdleAnim[(uint8_t)eB],
                        FaceConfig::kIdleAnim[(uint8_t)eC], l0, l1, l2);
}

}  // namespace EmotionBlend
