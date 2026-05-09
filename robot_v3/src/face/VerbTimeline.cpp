#include "VerbTimeline.h"

#include <math.h>
#include <string.h>

#include "FACE_CONFIG.h"

namespace Face {

namespace {

constexpr uint8_t kFieldCount = (uint8_t)FieldIndex::Count;

const FaceConfig::SparseVerbTimeline* tableFor(Expression v) {
  for (size_t i = 0; i < FaceConfig::kVerbTimelineCount; ++i) {
    if (FaceConfig::kVerbTimelines[i].verb == v) {
      return &FaceConfig::kVerbTimelines[i];
    }
  }
  return nullptr;
}

bool isVerbExpression(Expression e) {
  switch (e) {
    case Expression::VerbThinking:
    case Expression::VerbReading:
    case Expression::VerbWriting:
    case Expression::VerbExecuting:
    case Expression::VerbStraining:
    case Expression::VerbSleeping:
      return true;
    default:
      return false;
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
  if (isVerbExpression(sToVerb)) {
    sampleVerbTimeline(sToVerb, timeInVerbMs, toHas, toVals);
  }

  const uint32_t elapsed = nowMs - sTransitionStartMs;
  const float t =
      clamp01f((float)elapsed / (float)kVerbTransitionDurMs);

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
      const long s =
          lroundf((float)sFromSnapshot.strength[i] * oneMinus);
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

void sampleVerbTimeline(Expression verb, uint32_t /*time_in_verb_ms*/,
                        bool* hasField, ParamI16* fieldVals) {
  memset(hasField, 0, kFieldCount * sizeof(bool));
  memset(fieldVals, 0, kFieldCount * sizeof(ParamI16));

  const FaceConfig::SparseVerbTimeline* tab = tableFor(verb);
  if (!tab) return;

  for (uint8_t i = 0; i < tab->count; ++i) {
    const uint8_t fi = tab->o[i].field;
    if (fi >= kFieldCount) continue;
    hasField[fi] = true;
    fieldVals[fi] = ParamI16{tab->o[i].value, tab->o[i].strength};
  }
}

void sampleEffectiveVerb(Expression currentVerbExpression, uint32_t nowMs,
                         uint32_t timeInVerbMs, bool* hasField,
                         ParamI16* fieldVals) {
  if (currentVerbExpression != sToVerb) {
    // Snapshot the current effective output using the prior state, then
    // retarget. The snapshot becomes the "from" side of the new fade.
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
