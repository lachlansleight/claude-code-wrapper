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
    case FieldIndex::EyeTopApex:
      return p.eye_top_apex;
    case FieldIndex::EyeTopCorner:
      return p.eye_top_corner;
    case FieldIndex::EyeBotApex:
      return p.eye_bot_apex;
    case FieldIndex::EyeBotCorner:
      return p.eye_bot_corner;
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
    case FieldIndex::MouthTopApex:
      return p.mouth_top_apex;
    case FieldIndex::MouthTopCorner:
      return p.mouth_top_corner;
    case FieldIndex::MouthBotApex:
      return p.mouth_bot_apex;
    case FieldIndex::MouthBotCorner:
      return p.mouth_bot_corner;
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
    default:
      return p.eye_dy;
  }
}

ParamI16& fieldMutRef(FaceParams& p, FieldIndex i) {
  return const_cast<ParamI16&>(fieldConstRef(p, i));
}

ParamI16 combineEmotionVerbField(const ParamI16& e, bool hasVerb, const ParamI16& v) {
  if (!hasVerb) return e;
  ParamI16 out{};
  const uint32_t se = e.strength;
  const uint32_t sv = v.strength;
  out.strength = (uint8_t)(se > sv ? se : sv);
  if (out.strength > 100) out.strength = 100;
  if (se + sv > 0) {
    out.value = (int16_t)(((int32_t)e.value * (int32_t)se + (int32_t)v.value * (int32_t)sv) /
                          (int32_t)(se + sv));
  } else {
    out.value = e.value;
    out.strength = 0;
  }
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
