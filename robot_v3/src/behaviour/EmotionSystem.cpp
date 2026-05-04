#include "EmotionSystem.h"

#include <math.h>
#include <string.h>

namespace EmotionSystem {

namespace {

static constexpr float kTauMsA = 6000.0f;
static constexpr float kTauMsV = 90000.0f;
static constexpr float kSnapHysteresisDist = 0.05f;
static constexpr uint32_t kSnapHysteresisHoldMs = 100;
struct Coord {
  float v;
  float a;
};

// Axis-aligned region in (valence, activation); corners normalized to min/max per axis.
struct Box {
  float minV;
  float maxV;
  float minA;
  float maxA;
};

struct Driver {
  bool active;
  uint8_t id;
  float targetV;
};

Emotion sRaw = {0.0f, 0.0f};
Driver sDrivers[kMaxHeldDrivers];
uint32_t sLastTickMs = 0;
NamedEmotion sCurrentSnap = NamedEmotion::Neutral;
NamedEmotion sPendingSnap = NamedEmotion::Neutral;
uint32_t sPendingSnapSinceMs = 0;

// Bounding boxes from corner pairs {{v,a},{v,a}}; overlap ties break by kPickOrder.
constexpr Box kBoxes[(size_t)NamedEmotion::Count] = {
    {-0.25f, 0.25f, -1.0f, 1.0f},    // Neutral
    {0.25f, 1.0f, -1.0f, 0.25f},     // Happy
    {0.25f, 1.0f, 0.25f, 0.75f},     // Excited
    {0.25f, 1.0f, 0.75f, 1.0f},     // Joyful
    {-1.0f, -0.25f, -1.0f, 1.0f},    // Sad
};

static constexpr NamedEmotion kPickOrder[] = {
    NamedEmotion::Neutral,
    NamedEmotion::Happy,
    NamedEmotion::Excited,
    NamedEmotion::Joyful,
    NamedEmotion::Sad,
};

Coord boxCenter(const Box& b) {
  return Coord{
      (b.minV + b.maxV) * 0.5f,
      (b.minA + b.maxA) * 0.5f,
  };
}

bool inBox(float v, float a, const Box& b) {
  return v >= b.minV && v <= b.maxV && a >= b.minA && a <= b.maxA;
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

float distSq(float v, float a, const Coord& c) {
  const float dv = v - c.v;
  const float da = a - c.a;
  return dv * dv + da * da;
}

NamedEmotion nearestEmotionByCenter(float v, float a, float* outDistSq = nullptr) {
  NamedEmotion best = NamedEmotion::Neutral;
  float bestD = distSq(v, a, boxCenter(kBoxes[0]));
  for (size_t i = 1; i < (size_t)NamedEmotion::Count; ++i) {
    const float d = distSq(v, a, boxCenter(kBoxes[i]));
    if (d < bestD) {
      bestD = d;
      best = (NamedEmotion)i;
    }
  }
  if (outDistSq) *outDistSq = bestD;
  return best;
}

// Pick the first region (in kPickOrder) that contains (v,a); if none, fall back to nearest box center.
NamedEmotion emotionForPoint(float v, float a, float* outBestDist = nullptr) {
  for (NamedEmotion e : kPickOrder) {
    if (inBox(v, a, kBoxes[(size_t)e])) {
      if (outBestDist) {
        const Coord c = boxCenter(kBoxes[(size_t)e]);
        *outBestDist = sqrtf(distSq(v, a, c));
      }
      return e;
    }
  }
  float bestD = 0.0f;
  const NamedEmotion byCenter = nearestEmotionByCenter(v, a, &bestD);
  if (outBestDist) *outBestDist = sqrtf(bestD);
  return byCenter;
}

}  // namespace

void begin() {
  memset(sDrivers, 0, sizeof(sDrivers));
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
  const float targetV = activeTargetV();

  sRaw.activation = clampf(sRaw.activation + (0.0f - sRaw.activation) * alphaA, 0.0f, 1.0f);
  sRaw.valence = clampf(sRaw.valence + (targetV - sRaw.valence) * alphaV, -1.0f, 1.0f);

  float bestDist = 0.0f;
  const NamedEmotion nearest =
      emotionForPoint(sRaw.valence, sRaw.activation, &bestDist);
  if (nearest == sCurrentSnap) {
    sPendingSnap = nearest;
    sPendingSnapSinceMs = 0;
    return;
  }

  const Coord currentCenter = boxCenter(kBoxes[(size_t)sCurrentSnap]);
  const float currentDist =
      sqrtf(distSq(sRaw.valence, sRaw.activation, currentCenter));
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
  sRaw.valence = clampf(sRaw.valence + dValence, -1.0f, 1.0f);
  sRaw.activation = clampf(sRaw.activation + dActivation, 0.0f, 1.0f);
}

void setValence(float value) { sRaw.valence = clampf(value, -1.0f, 1.0f); }
void setArousal(float value) { sRaw.activation = clampf(value, 0.0f, 1.0f); }
void modifyValence(float delta) { setValence(sRaw.valence + delta); }
void modifyArousal(float delta) { setArousal(sRaw.activation + delta); }

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
    default:
      return "?";
  }
}

}  // namespace EmotionSystem
