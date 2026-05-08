#include "EmotionSystem.h"

#include <math.h>
#include <string.h>

namespace EmotionSystem {

namespace {

static constexpr float kTauMsA = 6000.0f;
static constexpr float kTauMsV = 90000.0f;
static constexpr float kTauMsRawFollow = 50.0f;
static constexpr float kSnapHysteresisDist = 0.05f;
static constexpr uint32_t kSnapHysteresisHoldMs = 100;
/// Squared-distance tolerance for tying two anchors (float noise).
static constexpr float kDistSqTieEps = 1e-7f;

struct EmotionPoint {
  float v;
  float a;
};

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

// Anchor samples in (valence, activation). Used for discrete snap + triangulation
// source (see scripts/gen_emotion_triangulation.py).
constexpr EmotionPoint kEmotionPoints[(size_t)NamedEmotion::Count] = {
    {+0.0f, 0.5f},    // Neutral
    {+0.5f, 0.5f},    // Happy
    {+1.0f, 0.6f},    // Excited
    {+1.0f, 1.0f},    // Joyful
    {-0.5f, 0.5f},    // Sad
    {-0.2f, 0.0f},    // Sleepy
    {-1.0f, 1.0f},    // Distressed
    {+1.0f, 0.0f},    // Blissed
    {-1.0f, 0.0f},    // Depressed
    {-0.3f, 1.0f},    // Shocked
    {-1.0f, 0.3f},    // Disappointed
    {+0.5f, 0.7f},    // Cheeky
    {+0.6f, 1.0f},    // Gleeful
    {-0.6f, 0.8f},    // Frustrated
};

// When two anchors are equidistant, first listed here wins (matches gen script
// anchor dedup: first emotion keeps the shared coordinate).
// Full permutation: tie-break priority when two anchors are equidistant.
static constexpr NamedEmotion kPickOrder[] = {
    NamedEmotion::Gleeful,
    NamedEmotion::Cheeky,
    NamedEmotion::Sleepy,
    NamedEmotion::Distressed,
    NamedEmotion::Frustrated,
    NamedEmotion::Disappointed,
    NamedEmotion::Blissed,
    NamedEmotion::Depressed,
    NamedEmotion::Shocked,
    NamedEmotion::Neutral,
    NamedEmotion::Happy,
    NamedEmotion::Excited,
    NamedEmotion::Joyful,
    NamedEmotion::Sad,
};

float distSq(float v, float a, const EmotionPoint& p) {
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

// Nearest anchor in (v, a); ties break by kPickOrder (first wins).
NamedEmotion emotionForPoint(float v, float a, float* outBestDist = nullptr) {
  float bestD = distSq(v, a, kEmotionPoints[0]);
  for (size_t i = 1; i < (size_t)NamedEmotion::Count; ++i) {
    const float d = distSq(v, a, kEmotionPoints[i]);
    if (d < bestD) bestD = d;
  }
  for (NamedEmotion e : kPickOrder) {
    const float d = distSq(v, a, kEmotionPoints[(size_t)e]);
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
  const NamedEmotion nearest =
      emotionForPoint(sRaw.valence, sRaw.activation, &bestDist);
  if (nearest == sCurrentSnap) {
    sPendingSnap = nearest;
    sPendingSnapSinceMs = 0;
    return;
  }

  const float currentDist =
      sqrtf(distSq(sRaw.valence, sRaw.activation, kEmotionPoints[(size_t)sCurrentSnap]));
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

const char* emotionName(NamedEmotion e) {
  switch (e) {
    case NamedEmotion::Neutral:
      return "neutral";
    case NamedEmotion::Happy:
      return "happy";
    case NamedEmotion::Excited:
      return "excited";
    case NamedEmotion::Joyful:
      return "joyful";
    case NamedEmotion::Sad:
      return "sad";
    case NamedEmotion::Sleepy:
      return "sleepy";
    case NamedEmotion::Distressed:
      return "distressed";
    case NamedEmotion::Blissed:
      return "blissed";
    case NamedEmotion::Depressed:
      return "depressed";
    case NamedEmotion::Shocked:
      return "shocked";
    case NamedEmotion::Disappointed:
      return "disappointed";
    case NamedEmotion::Cheeky:
      return "cheeky";
    case NamedEmotion::Gleeful:
      return "gleeful";
    case NamedEmotion::Frustrated:
      return "frustrated";
    default:
      return "?";
  }
}

}  // namespace EmotionSystem
