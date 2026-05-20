#include "SceneTypes.h"

#include <math.h>

#include "FACE_CONFIG.h"

namespace Face {

const char* expressionName(Expression e) { return FaceConfig::expressionName(e); }

const ParamI16& fieldConstRef(const FaceParams& p, FieldIndex i) {
  switch (i) {
    case FieldIndex::EyeDy:
      return p.eye_dy;
    case FieldIndex::EyeRx:
      return p.eye_rx;
    case FieldIndex::EyeOpenAmt:
      return p.eye_open_amt;
    case FieldIndex::EyeArcAmt:
      return p.eye_arc_amt;
    case FieldIndex::EyeThick:
      return p.eye_thick;
    case FieldIndex::EyeWaveAmp:
      return p.eye_wave_amp;
    case FieldIndex::EyeWaveFreq:
      return p.eye_wave_freq;
    case FieldIndex::EyeWaveSpeed:
      return p.eye_wave_speed;
    case FieldIndex::PupilDx:
      return p.pupil_dx;
    case FieldIndex::PupilDy:
      return p.pupil_dy;
    case FieldIndex::PupilR:
      return p.pupil_r;
    case FieldIndex::MouthDy:
      return p.mouth_dy;
    case FieldIndex::MouthRx:
      return p.mouth_rx;
    case FieldIndex::MouthOpenAmt:
      return p.mouth_open_amt;
    case FieldIndex::MouthArcAmt:
      return p.mouth_arc_amt;
    case FieldIndex::MouthThick:
      return p.mouth_thick;
    case FieldIndex::MouthWaveAmp:
      return p.mouth_wave_amp;
    case FieldIndex::MouthWaveFreq:
      return p.mouth_wave_freq;
    case FieldIndex::MouthWaveSpeed:
      return p.mouth_wave_speed;
    case FieldIndex::FaceRot:
      return p.face_rot;
    case FieldIndex::FaceY:
      return p.face_y;
    case FieldIndex::RingR:
      return p.ring_r;
    case FieldIndex::RingG:
      return p.ring_g;
    case FieldIndex::RingB:
      return p.ring_b;
    case FieldIndex::ArmMinDeg:
      return p.arm_min_deg;
    case FieldIndex::ArmMaxDeg:
      return p.arm_max_deg;
    case FieldIndex::ArmPeriodMs:
      return p.arm_period_ms;
    case FieldIndex::ArmIntervalMs:
      return p.arm_interval_ms;
    default:
      return p.eye_dy;
  }
}

ParamI16& fieldMutRef(FaceParams& p, FieldIndex i) {
  return const_cast<ParamI16&>(fieldConstRef(p, i));
}

ParamI16 combineEmotionVerbField(const ParamI16& e, bool hasVerb, const ParamI16& v) {
  if (!hasVerb) return e;

  const uint32_t se = e.strength;
  const uint32_t sv = v.strength;

  if (se == 0 && sv == 0) return ParamI16{0, 0};
  if (sv == 0) return e;
  if (se == 0) return v;

  // Lerp from emotion value to verb value. Verb strength is the lerp t
  // (so verb_str=0 keeps emotion, verb_str=100 takes verb). Emotion
  // strength shapes the curve: es=50 is linear, es<50 is ease-out (verb
  // dominates more easily), es>50 is ease-in (emotion holds on harder).
  // Power continuously varies from 1.0 (linear) at es=50 to kMaxPower
  // at es=0 or es=100.
  const float t = (float)sv / 100.0f;
  constexpr float kMaxPower = 5.0f;

  float factor;
  if (se == 50) {
    factor = t;
  } else if (se < 50) {
    const float power = 1.0f + (50.0f - (float)se) / 50.0f * (kMaxPower - 1.0f);
    factor = 1.0f - powf(1.0f - t, power);
  } else {
    const float power = 1.0f + ((float)se - 50.0f) / 50.0f * (kMaxPower - 1.0f);
    factor = powf(t, power);
  }

  ParamI16 out{};
  out.value =
      (int16_t)lroundf((float)e.value + ((float)v.value - (float)e.value) * factor);
  out.strength = (uint8_t)(se > sv ? se : sv);
  if (out.strength > 100) out.strength = 100;
  return out;
}

FaceParams combineEmotionVerbFace(const FaceParams& emotion, const bool* verbHas,
                                  const ParamI16* verbVals) {
  FaceParams out{};
  for (uint8_t i = 0; i < (uint8_t)FieldIndex::Count; ++i) {
    const FieldIndex fi = (FieldIndex)i;
    fieldMutRef(out, fi) =
        combineEmotionVerbField(fieldConstRef(emotion, fi), verbHas[i], verbVals[i]);
  }
  return out;
}

void smoothFaceValuesToward(FaceParams& state, const FaceParams& target, float alpha) {
  if (alpha < 0.0f) alpha = 0.0f;
  if (alpha > 1.0f) alpha = 1.0f;
  for (uint8_t i = 0; i < (uint8_t)FieldIndex::Count; ++i) {
    const FieldIndex fi = (FieldIndex)i;
    ParamI16& s = fieldMutRef(state, fi);
    const ParamI16& t = fieldConstRef(target, fi);
    s.strength = t.strength;
    s.value = (int16_t)lroundf((float)s.value + ((float)t.value - (float)s.value) * alpha);
  }
}

}  // namespace Face
