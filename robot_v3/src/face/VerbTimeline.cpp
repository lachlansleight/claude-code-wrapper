#include "VerbTimeline.h"

#include <math.h>
#include <string.h>

#include "FACE_CONFIG.h"

namespace Face {

bool expressionUsesVerbTimeline(Expression e) {
  switch (e) {
    case Expression::VerbThinking:
    case Expression::VerbReading:
    case Expression::VerbWriting:
    case Expression::VerbExecuting:
    case Expression::VerbStraining:
    case Expression::VerbSleeping:
    case Expression::VerbWaking:
    case Expression::VerbAttractingAttention:
      return true;
    default:
      return false;
  }
}

void sampleVerbTimeline(Expression verb, uint32_t time_in_verb_ms, bool* hasField,
                        ParamI16* fieldVals);

namespace {

constexpr uint8_t kFieldCount = (uint8_t)FieldIndex::Count;

const FaceConfig::VerbTimeline* tableFor(Expression v) {
  for (size_t i = 0; i < FaceConfig::kVerbTimelineCount; ++i) {
    if (FaceConfig::kVerbTimelines[i].verb == v) {
      return &FaceConfig::kVerbTimelines[i];
    }
  }
  return nullptr;
}

// Effective state after applying keyframes 0..uptoIdx in order. Later
// overrides replace the same field; strength 0 clears (verb relinquishes).
void cumulativeState(const FaceConfig::VerbTimeline* tab, uint8_t uptoIdx, bool* hasField,
                     ParamI16* fieldVals) {
  memset(hasField, 0, kFieldCount * sizeof(bool));
  memset(fieldVals, 0, kFieldCount * sizeof(ParamI16));
  if (!tab || uptoIdx >= tab->keyframe_count) return;
  for (uint8_t j = 0; j <= uptoIdx; ++j) {
    const FaceConfig::VerbKeyframe& kf = tab->keyframes[j];
    for (uint8_t i = 0; i < kf.override_count && i < FaceConfig::kVerbKeyframeOverridesMax; ++i) {
      const uint8_t f = kf.overrides[i].field;
      if (f >= kFieldCount) continue;
      if (kf.overrides[i].strength == 0) {
        hasField[f] = false;
      } else {
        hasField[f] = true;
        fieldVals[f] = ParamI16{kf.overrides[i].targetValue, kf.overrides[i].strength};
      }
    }
  }
}

void lerpFieldSnapshots(const bool* has0, const ParamI16* v0, const bool* has1,
                        const ParamI16* v1, float u, bool* outHas, ParamI16* outVals) {
  const float om = 1.0f - u;
  for (uint8_t fi = 0; fi < kFieldCount; ++fi) {
    const bool h0 = has0[fi];
    const bool h1 = has1[fi];
    if (!h0 && !h1) {
      outHas[fi] = false;
      outVals[fi] = ParamI16{0, 0};
    } else if (h0 && h1) {
      outHas[fi] = true;
      outVals[fi].value = (int16_t)lroundf((float)v0[fi].value * om + (float)v1[fi].value * u);
      outVals[fi].strength =
          (uint8_t)lroundf((float)v0[fi].strength * om + (float)v1[fi].strength * u);
    } else if (h0) {
      const long s = lroundf((float)v0[fi].strength * om);
      outHas[fi] = s > 0;
      outVals[fi].value = v0[fi].value;
      outVals[fi].strength = (uint8_t)(s < 0 ? 0 : s);
    } else {
      const long s = lroundf((float)v1[fi].strength * u);
      outHas[fi] = s > 0;
      outVals[fi].value = v1[fi].value;
      outVals[fi].strength = (uint8_t)(s < 0 ? 0 : s);
    }
  }
}

// Snapshot of the effective output frozen at the moment of the last verb
// change. Drives the "from" side of the cross-fade.
struct Snapshot {
  bool has[kFieldCount];
  int16_t value[kFieldCount];
  uint8_t strength[kFieldCount];
};

Snapshot sFromSnapshot;
Expression sToVerb = Expression::Count;  // sentinel: "no verb / empty target"
uint32_t sTransitionStartMs = 0;
bool sFromInitialised = false;

float clamp01f(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

// Render the current effective sample using the *current* state. Pure read
// of stateful values — does not mutate them. Used both by `sampleEffectiveVerb`
// directly (steady output) and by the change-edge branch (to capture the
// in-flight snapshot before retargeting).
void evaluate(uint32_t nowMs, uint32_t timeInVerbMs, bool* outHas, ParamI16* outVals) {
  bool toHas[kFieldCount];
  ParamI16 toVals[kFieldCount];
  memset(toHas, 0, sizeof(toHas));
  memset(toVals, 0, sizeof(toVals));
  if (expressionUsesVerbTimeline(sToVerb)) {
    sampleVerbTimeline(sToVerb, timeInVerbMs, toHas, toVals);
  }

  const uint32_t elapsed = nowMs - sTransitionStartMs;
  const float t = clamp01f((float)elapsed / (float)kVerbTransitionDurMs);

  if (!sFromInitialised || t >= 1.0f) {
    memcpy(outHas, toHas, sizeof(toHas));
    memcpy(outVals, toVals, sizeof(toVals));
    return;
  }

  const float oneMinus = 1.0f - t;
  for (uint8_t i = 0; i < kFieldCount; ++i) {
    const bool fromHas = sFromSnapshot.has[i];
    const bool nextHas = toHas[i];
    if (fromHas && nextHas) {
      const float v =
          (float)sFromSnapshot.value[i] * oneMinus + (float)toVals[i].value * t;
      const float s = (float)sFromSnapshot.strength[i] * oneMinus +
                      (float)toVals[i].strength * t;
      outHas[i] = true;
      outVals[i].value = (int16_t)lroundf(v);
      outVals[i].strength = (uint8_t)lroundf(s);
    } else if (fromHas) {
      const long s = lroundf((float)sFromSnapshot.strength[i] * oneMinus);
      outHas[i] = s > 0;
      outVals[i].value = sFromSnapshot.value[i];
      outVals[i].strength = (uint8_t)(s < 0 ? 0 : s);
    } else if (nextHas) {
      const long s = lroundf((float)toVals[i].strength * t);
      outHas[i] = s > 0;
      outVals[i].value = toVals[i].value;
      outVals[i].strength = (uint8_t)(s < 0 ? 0 : s);
    } else {
      outHas[i] = false;
      outVals[i].value = 0;
      outVals[i].strength = 0;
    }
  }
}

}  // namespace

void sampleVerbTimeline(Expression verb, uint32_t time_in_verb_ms, bool* hasField,
                        ParamI16* fieldVals) {
  memset(hasField, 0, kFieldCount * sizeof(bool));
  memset(fieldVals, 0, kFieldCount * sizeof(ParamI16));

  const FaceConfig::VerbTimeline* tab = tableFor(verb);
  if (!tab || tab->keyframe_count == 0) return;

  const uint8_t K = tab->keyframe_count;
  const uint32_t L = tab->loop_duration_ms;

  if (K == 1u) {
    cumulativeState(tab, 0, hasField, fieldVals);
    return;
  }
  if (L == 0u) {
    cumulativeState(tab, (uint8_t)(K - 1u), hasField, fieldVals);
    return;
  }

  const uint32_t t_mod = time_in_verb_ms % L;

  uint8_t i0 = 0;
  uint8_t i1 = 0;
  float u = 0.0f;

  const FaceConfig::VerbKeyframe* kfs = tab->keyframes;
  if (t_mod >= kfs[K - 1u].time_ms) {
    i0 = (uint8_t)(K - 1u);
    i1 = 0;
    const uint32_t span = L - kfs[K - 1u].time_ms + kfs[0].time_ms;
    u = span > 0u ? (float)(t_mod - kfs[K - 1u].time_ms) / (float)span : 0.0f;
  } else {
    uint8_t seg = 0;
    for (; seg + 1u < K; ++seg) {
      if (t_mod < kfs[seg + 1u].time_ms) break;
    }
    i0 = seg;
    i1 = (uint8_t)(seg + 1u);
    const uint32_t t0 = kfs[i0].time_ms;
    const uint32_t t1 = kfs[i1].time_ms;
    const uint32_t dt = t1 - t0;
    u = dt > 0u ? (float)(t_mod - t0) / (float)dt : 0.0f;
  }

  bool leftHas[kFieldCount];
  ParamI16 leftVals[kFieldCount];
  bool rightHas[kFieldCount];
  ParamI16 rightVals[kFieldCount];
  cumulativeState(tab, i0, leftHas, leftVals);
  cumulativeState(tab, i1, rightHas, rightVals);
  lerpFieldSnapshots(leftHas, leftVals, rightHas, rightVals, u, hasField, fieldVals);
}

void sampleEffectiveVerb(Expression currentVerbExpression, uint32_t nowMs,
                         uint32_t timeInVerbMs, bool* hasField, ParamI16* fieldVals) {
  if (currentVerbExpression != sToVerb) {
    bool tmpHas[kFieldCount];
    ParamI16 tmpVals[kFieldCount];
    evaluate(nowMs, timeInVerbMs, tmpHas, tmpVals);
    for (uint8_t i = 0; i < kFieldCount; ++i) {
      sFromSnapshot.has[i] = tmpHas[i];
      sFromSnapshot.value[i] = tmpVals[i].value;
      sFromSnapshot.strength[i] = tmpVals[i].strength;
    }
    sFromInitialised = true;
    sToVerb = currentVerbExpression;
    sTransitionStartMs = nowMs;
  }

  evaluate(nowMs, timeInVerbMs, hasField, fieldVals);
}

void resetVerbTransition() {
  memset(&sFromSnapshot, 0, sizeof(sFromSnapshot));
  sToVerb = Expression::Count;
  sTransitionStartMs = 0;
  sFromInitialised = false;
}

float verbTransitionT(uint32_t nowMs) {
  if (!sFromInitialised) return 1.0f;
  const uint32_t elapsed = nowMs - sTransitionStartMs;
  return clamp01f((float)elapsed / (float)kVerbTransitionDurMs);
}

}  // namespace Face
