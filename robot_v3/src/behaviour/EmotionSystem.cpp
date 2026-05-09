#include "EmotionSystem.h"

#include <math.h>
#include <string.h>

#include "../face/FACE_CONFIG.h"

namespace EmotionSystem {

namespace {

static constexpr float kTauMsA = 6000.0f;
static constexpr float kTauMsV = 90000.0f;
static constexpr float kTauMsRawFollow = 50.0f;
static constexpr float kSnapHysteresisDist = 0.05f;
static constexpr uint32_t kSnapHysteresisHoldMs = 100;
/// Squared-distance tolerance for tying two anchors (float noise).
static constexpr float kDistSqTieEps = 1e-7f;

struct Driver {
  bool active;
  uint8_t id;
  float targetV;
};

Emotion sGoal = {0.0f, 0.0f};
Emotion sRaw = {0.0f, 0.0f};
Driver sDrivers[kMaxHeldDrivers];
uint32_t sLastTickMs = 0;
NamedEmotion sCurrentSnap = NamedEmotion::Neutral;
NamedEmotion sPendingSnap = NamedEmotion::Neutral;
uint32_t sPendingSnapSinceMs = 0;

float distSq(float v, float a, const FaceConfig::EmotionPoint& p) {
  const float dv = v - p.v;
  const float da = a - p.a;
  return dv * dv + da * da;
}

float clampf(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

float activeTargetV() {
  float best = 0.0f;
  float bestMag = 0.0f;
  for (size_t i = 0; i < kMaxHeldDrivers; ++i) {
    if (!sDrivers[i].active) continue;
    const float mag = fabsf(sDrivers[i].targetV);
    if (mag > bestMag) {
      bestMag = mag;
      best = sDrivers[i].targetV;
    }
  }
  return best;
}

// Nearest anchor in (v, a); ties break by FaceConfig::kPickOrder (first wins).
NamedEmotion emotionForPoint(float v, float a, float* outBestDist = nullptr) {
  float bestD = distSq(v, a, FaceConfig::kEmotionPoints[0]);
  for (size_t i = 1; i < (size_t)NamedEmotion::Count; ++i) {
    const float d = distSq(v, a, FaceConfig::kEmotionPoints[i]);
    if (d < bestD) bestD = d;
  }
  for (NamedEmotion e : FaceConfig::kPickOrder) {
    const float d = distSq(v, a, FaceConfig::kEmotionPoints[(size_t)e]);
    if (d <= bestD + kDistSqTieEps) {
      if (outBestDist) *outBestDist = sqrtf(bestD);
      return e;
    }
  }
  if (outBestDist) *outBestDist = sqrtf(bestD);
  return NamedEmotion::Neutral;
}

}  // namespace

void begin() {
  memset(sDrivers, 0, sizeof(sDrivers));
  sGoal = {0.0f, 0.0f};
  sRaw = {0.0f, 0.0f};
  sLastTickMs = millis();
  sCurrentSnap = NamedEmotion::Neutral;
  sPendingSnap = NamedEmotion::Neutral;
  sPendingSnapSinceMs = 0;
}

void tick() {
  const uint32_t now = millis();
  const uint32_t dtMs = (sLastTickMs == 0) ? 0 : (now - sLastTickMs);
  sLastTickMs = now;
  if (dtMs == 0) return;

  const float alphaA = 1.0f - expf(-(float)dtMs / kTauMsA);
  const float alphaV = 1.0f - expf(-(float)dtMs / kTauMsV);
  const float alphaFollow = 1.0f - expf(-(float)dtMs / kTauMsRawFollow);
  const float targetV = activeTargetV();

  const float newActivation =
      fmaxf(0.5f, clampf(sGoal.activation + (0.0f - sGoal.activation) * alphaA, 0.0f, 1.0f));
  sGoal.activation = fminf(sGoal.activation, newActivation);
  sGoal.valence = clampf(sGoal.valence + (targetV - sGoal.valence) * alphaV, -1.0f, 1.0f);

  sRaw.valence =
      clampf(sRaw.valence + (sGoal.valence - sRaw.valence) * alphaFollow, -1.0f, 1.0f);
  sRaw.activation =
      clampf(sRaw.activation + (sGoal.activation - sRaw.activation) * alphaFollow, 0.0f, 1.0f);

  float bestDist = 0.0f;
  const NamedEmotion nearest = emotionForPoint(sRaw.valence, sRaw.activation, &bestDist);
  if (nearest == sCurrentSnap) {
    sPendingSnap = nearest;
    sPendingSnapSinceMs = 0;
    return;
  }

  const float currentDist =
      sqrtf(distSq(sRaw.valence, sRaw.activation, FaceConfig::kEmotionPoints[(size_t)sCurrentSnap]));
  if (currentDist - bestDist <= kSnapHysteresisDist) return;

  if (sPendingSnap != nearest) {
    sPendingSnap = nearest;
    sPendingSnapSinceMs = now;
    return;
  }

  if (sPendingSnapSinceMs != 0 && (now - sPendingSnapSinceMs) >= kSnapHysteresisHoldMs) {
    sCurrentSnap = nearest;
    sPendingSnapSinceMs = 0;
  }
}

void impulse(float dValence, float dActivation) {
  sGoal.valence = clampf(sGoal.valence + dValence, -1.0f, 1.0f);
  sGoal.activation = clampf(sGoal.activation + dActivation, 0.0f, 1.0f);
}

void setValence(float value) { sGoal.valence = clampf(value, -1.0f, 1.0f); }
void setArousal(float value) { sGoal.activation = clampf(value, 0.0f, 1.0f); }
void modifyValence(float delta) { setValence(sGoal.valence + delta); }
void modifyArousal(float delta) { setArousal(sGoal.activation + delta); }

void setHeldTarget(uint8_t driverId, float targetValence) {
  for (size_t i = 0; i < kMaxHeldDrivers; ++i) {
    if (sDrivers[i].active && sDrivers[i].id == driverId) {
      sDrivers[i].targetV = clampf(targetValence, -1.0f, 1.0f);
      return;
    }
  }
  for (size_t i = 0; i < kMaxHeldDrivers; ++i) {
    if (!sDrivers[i].active) {
      sDrivers[i].active = true;
      sDrivers[i].id = driverId;
      sDrivers[i].targetV = clampf(targetValence, -1.0f, 1.0f);
      return;
    }
  }
}

void releaseHeldTarget(uint8_t driverId) {
  for (size_t i = 0; i < kMaxHeldDrivers; ++i) {
    if (sDrivers[i].active && sDrivers[i].id == driverId) {
      sDrivers[i].active = false;
      return;
    }
  }
}

Emotion raw() { return sRaw; }

SnappedEmotion snapped() {
  return SnappedEmotion{
      sCurrentSnap,
      sRaw.valence,
      sRaw.activation,
  };
}

DebugState debugState() {
  DebugState out = {};
  out.snappedCurrent = sCurrentSnap;
  out.snappedPending = sPendingSnap;
  out.pendingSnapActive = (sPendingSnapSinceMs != 0);
  out.pendingSnapSinceMs = sPendingSnapSinceMs;
  for (size_t i = 0; i < kMaxHeldDrivers; ++i) {
    if (!sDrivers[i].active) continue;
    if (out.heldDriverCount >= kMaxHeldDrivers) break;
    out.heldDrivers[out.heldDriverCount].id = sDrivers[i].id;
    out.heldDrivers[out.heldDriverCount].targetValence = sDrivers[i].targetV;
    out.heldDriverCount++;
  }
  return out;
}

const char* emotionName(NamedEmotion e) { return FaceConfig::emotionName(e); }

}  // namespace EmotionSystem
