#pragma once

#include <Arduino.h>

namespace EmotionSystem {

enum class NamedEmotion : uint8_t {
  Neutral = 0,
  Happy,
  Excited,
  Joyful,
  Sad,
  Count
};

struct Emotion {
  float valence;
  float activation;
};

struct SnappedEmotion {
  NamedEmotion named;
  float valence;
  float activation;
};

static constexpr uint8_t kMaxHeldDrivers = 8;

struct HeldValenceDriver {
  uint8_t id;
  float targetValence;
};

struct DebugState {
  NamedEmotion snappedCurrent;
  NamedEmotion snappedPending;
  bool pendingSnapActive;
  uint32_t pendingSnapSinceMs;
  uint8_t heldDriverCount;
  HeldValenceDriver heldDrivers[kMaxHeldDrivers];
};

namespace Drivers {
static constexpr uint8_t PendingPermission = 1;
static constexpr uint8_t Straining = 2;
}

void begin();
void tick();

void impulse(float dValence, float dActivation);
void setValence(float value);
void setArousal(float value);
void modifyValence(float delta);
void modifyArousal(float delta);

void setHeldTarget(uint8_t driverId, float targetValence);
void releaseHeldTarget(uint8_t driverId);

Emotion raw();
SnappedEmotion snapped();
DebugState debugState();
const char* emotionName(NamedEmotion e);

}  // namespace EmotionSystem
