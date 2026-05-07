#include "EmotionBlend.h"

#include <math.h>

#include "../face/FrameController.h"
#include "EmotionSystem.h"
#include "EmotionTriangulation.h"

namespace EmotionBlend {

namespace {

constexpr float kBaryEps = 1e-5f;

Face::Expression expressionForNamedEmotion(EmotionSystem::NamedEmotion e) {
  switch (e) {
    case EmotionSystem::NamedEmotion::Happy:
      return Face::Expression::Happy;
    case EmotionSystem::NamedEmotion::Excited:
      return Face::Expression::Excited;
    case EmotionSystem::NamedEmotion::Joyful:
      return Face::Expression::Joyful;
    case EmotionSystem::NamedEmotion::Sad:
      return Face::Expression::Sad;
    case EmotionSystem::NamedEmotion::Sleepy:
      return Face::Expression::Sleepy;
    case EmotionSystem::NamedEmotion::Distressed:
      return Face::Expression::Distressed;
    case EmotionSystem::NamedEmotion::Blissed:
      return Face::Expression::Blissed;
    case EmotionSystem::NamedEmotion::Depressed:
      return Face::Expression::Depressed;
    case EmotionSystem::NamedEmotion::Shocked:
      return Face::Expression::Shocked;
    case EmotionSystem::NamedEmotion::Disappointed:
      return Face::Expression::Disappointed;
    case EmotionSystem::NamedEmotion::Neutral:
    default:
      return Face::Expression::Neutral;
  }
}

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

int16_t blendField(int16_t a, int16_t b, int16_t c, float la, float lb, float lc) {
  const float v = (float)a * la + (float)b * lb + (float)c * lc;
  return (int16_t)lroundf(v);
}

struct ArmPreset {
  int16_t min_deg;
  int16_t max_deg;
  float period_s;
  float interval_s;
};

static ArmPreset armPresetFor(Face::Expression e) {
  switch (e) {
    case Face::Expression::Neutral:
      return {-25, -15, 2.0f, 1.0f};
    case Face::Expression::Happy:
      return {-23, -7, 1.5f, 0.5f};
    case Face::Expression::Excited:
      return {-15, -5, 1.0f, 0.0f};
    case Face::Expression::Joyful:
      return {10, 25, 0.9f, 0.2f};
    case Face::Expression::Sad:
      return {-25, -15, 2.0f, 1.0f};
    case Face::Expression::Sleepy:
      return {-25, -20, 3.0f, 6.0f};
    case Face::Expression::Distressed:
      return {-15, -5, 1.0f, 0.0f};
    case Face::Expression::Blissed:
      return {-25, -20, 3.0f, 6.0f};
    case Face::Expression::Depressed:
      return {-25, -20, 3.0f, 6.0f};
    case Face::Expression::Shocked:
      return {-15, -5, 1.0f, 0.0f};
    case Face::Expression::Disappointed:
      return {-23, -7, 1.5f, 0.5f};
    default:
      return {-25, -15, 2.0f, 1.0f};
  }
}

static float blendFloat(float a, float b, float c, float la, float lb, float lc) {
  return a * la + b * lb + c * lc;
}

static Face::EmotionArmMotion blendArmThree(const ArmPreset& A, const ArmPreset& B,
                                            const ArmPreset& C, float la, float lb, float lc) {
  Face::EmotionArmMotion r;
  r.min_offset_deg = blendField(A.min_deg, B.min_deg, C.min_deg, la, lb, lc);
  r.max_offset_deg = blendField(A.max_deg, B.max_deg, C.max_deg, la, lb, lc);
  r.waggle_period_s = blendFloat(A.period_s, B.period_s, C.period_s, la, lb, lc);
  r.waggle_interval_s = blendFloat(A.interval_s, B.interval_s, C.interval_s, la, lb, lc);
  return r;
}

Face::FaceParams blendThree(const Face::FaceParams& A, const Face::FaceParams& B,
                            const Face::FaceParams& C, float la, float lb, float lc) {
  Face::FaceParams r;
  r.eye_dy = blendField(A.eye_dy, B.eye_dy, C.eye_dy, la, lb, lc);
  r.eye_rx = blendField(A.eye_rx, B.eye_rx, C.eye_rx, la, lb, lc);
  r.eye_top_apex = blendField(A.eye_top_apex, B.eye_top_apex, C.eye_top_apex, la, lb, lc);
  r.eye_top_corner = blendField(A.eye_top_corner, B.eye_top_corner, C.eye_top_corner, la, lb, lc);
  r.eye_bot_apex = blendField(A.eye_bot_apex, B.eye_bot_apex, C.eye_bot_apex, la, lb, lc);
  r.eye_bot_corner = blendField(A.eye_bot_corner, B.eye_bot_corner, C.eye_bot_corner, la, lb, lc);
  r.eye_thick = blendField(A.eye_thick, B.eye_thick, C.eye_thick, la, lb, lc);
  r.eye_wave_amp = blendField(A.eye_wave_amp, B.eye_wave_amp, C.eye_wave_amp, la, lb, lc);
  r.eye_wave_freq = blendField(A.eye_wave_freq, B.eye_wave_freq, C.eye_wave_freq, la, lb, lc);
  r.eye_wave_speed = blendField(A.eye_wave_speed, B.eye_wave_speed, C.eye_wave_speed, la, lb, lc);
  r.pupil_dx = blendField(A.pupil_dx, B.pupil_dx, C.pupil_dx, la, lb, lc);
  r.pupil_dy = blendField(A.pupil_dy, B.pupil_dy, C.pupil_dy, la, lb, lc);
  r.pupil_r = blendField(A.pupil_r, B.pupil_r, C.pupil_r, la, lb, lc);
  r.mouth_dy = blendField(A.mouth_dy, B.mouth_dy, C.mouth_dy, la, lb, lc);
  r.mouth_rx = blendField(A.mouth_rx, B.mouth_rx, C.mouth_rx, la, lb, lc);
  r.mouth_top_apex = blendField(A.mouth_top_apex, B.mouth_top_apex, C.mouth_top_apex, la, lb, lc);
  r.mouth_top_corner =
      blendField(A.mouth_top_corner, B.mouth_top_corner, C.mouth_top_corner, la, lb, lc);
  r.mouth_bot_apex = blendField(A.mouth_bot_apex, B.mouth_bot_apex, C.mouth_bot_apex, la, lb, lc);
  r.mouth_bot_corner =
      blendField(A.mouth_bot_corner, B.mouth_bot_corner, C.mouth_bot_corner, la, lb, lc);
  r.mouth_thick = blendField(A.mouth_thick, B.mouth_thick, C.mouth_thick, la, lb, lc);
  r.mouth_wave_amp = blendField(A.mouth_wave_amp, B.mouth_wave_amp, C.mouth_wave_amp, la, lb, lc);
  r.mouth_wave_freq =
      blendField(A.mouth_wave_freq, B.mouth_wave_freq, C.mouth_wave_freq, la, lb, lc);
  r.mouth_wave_speed =
      blendField(A.mouth_wave_speed, B.mouth_wave_speed, C.mouth_wave_speed, la, lb, lc);
  r.face_rot = blendField(A.face_rot, B.face_rot, C.face_rot, la, lb, lc);
  r.face_y = blendField(A.face_y, B.face_y, C.face_y, la, lb, lc);
  r.ring_r = blendField(A.ring_r, B.ring_r, C.ring_r, la, lb, lc);
  r.ring_g = blendField(A.ring_g, B.ring_g, C.ring_g, la, lb, lc);
  r.ring_b = blendField(A.ring_b, B.ring_b, C.ring_b, la, lb, lc);
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
        expressionForNamedEmotion(EmotionSystem::kAnchors[bestIdx].emotion));
  }

  const Face::Expression eA =
      expressionForNamedEmotion(EmotionSystem::kAnchors[i0].emotion);
  const Face::Expression eB =
      expressionForNamedEmotion(EmotionSystem::kAnchors[i1].emotion);
  const Face::Expression eC =
      expressionForNamedEmotion(EmotionSystem::kAnchors[i2].emotion);

  return blendThree(Face::baseTargetFor(eA), Face::baseTargetFor(eB),
                    Face::baseTargetFor(eC), l0, l1, l2);
}

Face::EmotionArmMotion blendedEmotionArmMotion(float v, float a) {
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
        expressionForNamedEmotion(EmotionSystem::kAnchors[bestIdx].emotion);
    const ArmPreset p = armPresetFor(ex);
    Face::EmotionArmMotion one;
    one.min_offset_deg = p.min_deg;
    one.max_offset_deg = p.max_deg;
    if (one.min_offset_deg > one.max_offset_deg) {
      const int16_t t = one.min_offset_deg;
      one.min_offset_deg = one.max_offset_deg;
      one.max_offset_deg = t;
    }
    one.waggle_period_s = p.period_s < 0.05f ? 0.05f : p.period_s;
    one.waggle_interval_s = p.interval_s < 0.0f ? 0.0f : p.interval_s;
    return one;
  }

  const Face::Expression eA =
      expressionForNamedEmotion(EmotionSystem::kAnchors[i0].emotion);
  const Face::Expression eB =
      expressionForNamedEmotion(EmotionSystem::kAnchors[i1].emotion);
  const Face::Expression eC =
      expressionForNamedEmotion(EmotionSystem::kAnchors[i2].emotion);

  Face::EmotionArmMotion r =
      blendArmThree(armPresetFor(eA), armPresetFor(eB), armPresetFor(eC), l0, l1, l2);
  if (r.min_offset_deg > r.max_offset_deg) {
    const int16_t t = r.min_offset_deg;
    r.min_offset_deg = r.max_offset_deg;
    r.max_offset_deg = t;
  }
  if (r.waggle_period_s < 0.05f) r.waggle_period_s = 0.05f;
  if (r.waggle_interval_s < 0.0f) r.waggle_interval_s = 0.0f;
  return r;
}

}  // namespace EmotionBlend
