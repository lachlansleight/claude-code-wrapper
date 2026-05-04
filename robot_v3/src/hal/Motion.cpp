#include "Motion.h"

#include <ESP32Servo.h>
#include <math.h>

#include "../config.h"
#include "../core/DebugLog.h"

namespace Motion {

namespace {

struct Keyframe {
  uint8_t angle;
  uint16_t dwellMs;
};

struct Pattern {
  const Keyframe* frames;
  uint8_t count;
};

static constexpr uint8_t kCentre = 90;
static constexpr uint32_t kJogWriteEvery = 20;
static constexpr uint32_t kThinkWriteEvery = 20;
static constexpr uint32_t kThinkEaseInMs = 1000;

Servo servo;
bool attached = false;
int8_t safeMin = -90;
int8_t safeMax = 90;

Keyframe waggleFrames[5];
Pattern waggle = {waggleFrames, 5};

const Pattern* playing = nullptr;
uint8_t frameIdx = 0;
uint32_t frameStartMs = 0;

bool jogActive = false;
uint8_t jogStartAngle = kCentre;
uint8_t jogTargetAngle = kCentre;
uint32_t jogStartMs = 0;
uint32_t lastJogWriteMs = 0;
uint32_t jogDurationMs = 250;

bool thinkingMode = false;
uint32_t lastThinkWriteMs = 0;
uint32_t thinkStartMs = 0;
uint8_t thinkStartAngle = kCentre;
int8_t thinkCenterOff = 0;
uint8_t thinkAmplitude = 5;
uint16_t thinkPeriodMs = 2000;

bool holdActive = false;
uint32_t holdUntilMs = 0;
bool holdExpiredEdge = false;

uint8_t commandedAngle = kCentre;

int8_t clampToSafe(int value) {
  if (value < (int)safeMin) value = safeMin;
  if (value > (int)safeMax) value = safeMax;
  return (int8_t)value;
}

uint8_t offsetToAngle(int8_t offsetDeg) {
  const int8_t safe = clampToSafe(offsetDeg);
  int target = (int)kCentre + (int)safe;
  if (target < 0) target = 0;
  if (target > 180) target = 180;
  return (uint8_t)target;
}

void writeAngle(uint8_t angle) {
  commandedAngle = angle;
  servo.write(angle);
}

void startPattern(const Pattern* p) {
  playing = p;
  frameIdx = 0;
  frameStartMs = millis();
  writeAngle(p->frames[0].angle);
}

void tickJog() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - jogStartMs;
  if (elapsed >= jogDurationMs) {
    writeAngle(jogTargetAngle);
    jogActive = false;
  } else if (now - lastJogWriteMs >= kJogWriteEvery) {
    lastJogWriteMs = now;
    const float t = (float)elapsed / (float)jogDurationMs;
    const float eased = t * t * (3.0f - 2.0f * t);
    const int delta = (int)jogTargetAngle - (int)jogStartAngle;
    const int angle = (int)jogStartAngle + (int)((float)delta * eased);
    writeAngle((uint8_t)angle);
  }
}

}  // namespace

void begin() {
  servo.setPeriodHertz(50);
  attached = servo.attach(SERVO_PIN, 500, 2400);
  if (!attached) {
    LOG_ERR("servo attach failed on pin %d", SERVO_PIN);
    return;
  }
  writeAngle(kCentre);
  LOG_INFO("servo ready on pin %d", SERVO_PIN);
}

void setSafeRange(int8_t minOffsetDeg, int8_t maxOffsetDeg) {
  if (minOffsetDeg > maxOffsetDeg) {
    const int8_t tmp = minOffsetDeg;
    minOffsetDeg = maxOffsetDeg;
    maxOffsetDeg = tmp;
  }
  if (minOffsetDeg < -90) minOffsetDeg = -90;
  if (maxOffsetDeg > 90) maxOffsetDeg = 90;
  safeMin = minOffsetDeg;
  safeMax = maxOffsetDeg;
  LOG_INFO("servo safe range = [%d, %d]", (int)safeMin, (int)safeMax);
}

void tick() {
  if (!attached) return;

  if (holdActive) {
    if ((int32_t)(millis() - holdUntilMs) >= 0) {
      holdActive = false;
      holdExpiredEdge = true;
    } else {
      if (jogActive) tickJog();
      return;
    }
  }

  if (jogActive) {
    tickJog();
    return;
  }

  if (playing) {
    const Keyframe& kf = playing->frames[frameIdx];
    if (millis() - frameStartMs < kf.dwellMs) return;
    frameIdx++;
    if (frameIdx >= playing->count) {
      playing = nullptr;
    } else {
      frameStartMs = millis();
      writeAngle(playing->frames[frameIdx].angle);
    }
    return;
  }

  if (!thinkingMode) return;
  const uint32_t now = millis();
  if (now - lastThinkWriteMs < kThinkWriteEvery) return;
  lastThinkWriteMs = now;

  const uint32_t sinceStart = now - thinkStartMs;
  float ease = 1.0f;
  if (sinceStart < kThinkEaseInMs) {
    const float t = (float)sinceStart / (float)kThinkEaseInMs;
    ease = t * t * (3.0f - 2.0f * t);
  }
  const float targetCentre = (float)offsetToAngle(thinkCenterOff);
  const float base = (float)thinkStartAngle + (targetCentre - (float)thinkStartAngle) * ease;
  const uint16_t period = thinkPeriodMs == 0 ? 1 : thinkPeriodMs;
  const float phase = (float)(now % period) / (float)period;
  const float delta = (float)thinkAmplitude * ease * sinf(phase * 2.0f * (float)PI);
  writeAngle(offsetToAngle((int8_t)((base + delta) - (float)kCentre)));
}

void playWaggle(int8_t center, uint8_t amplitude, uint16_t periodMs) {
  if (!attached) return;
  if (playing || jogActive) return;
  if (periodMs < 50) periodMs = 50;
  const uint16_t dwell = (uint16_t)(periodMs / 10);
  const uint8_t cAng = offsetToAngle(center);
  const uint8_t hiAng = offsetToAngle((int8_t)((int)center + (int)amplitude));
  const uint8_t loAng = offsetToAngle((int8_t)((int)center - (int)amplitude));
  waggleFrames[0] = {cAng, dwell};
  waggleFrames[1] = {hiAng, dwell};
  waggleFrames[2] = {loAng, dwell};
  waggleFrames[3] = {hiAng, dwell};
  waggleFrames[4] = {cAng, dwell};
  startPattern(&waggle);
}

void cancelAll() {
  playing = nullptr;
  jogActive = false;
  thinkingMode = false;
}

void playJog(int8_t offsetDeg, uint16_t durationMs) {
  if (!attached) return;
  jogStartAngle = commandedAngle;
  jogTargetAngle = offsetToAngle(offsetDeg);
  jogStartMs = millis();
  lastJogWriteMs = 0;
  jogDurationMs = durationMs > 0 ? durationMs : 1;
  jogActive = true;
  playing = nullptr;
}

void setThinkingMode(bool on, int8_t centerOffset, uint8_t amplitude, uint16_t periodMs) {
  if (!on) {
    thinkingMode = false;
    return;
  }
  thinkingMode = true;
  thinkStartMs = millis();
  thinkStartAngle = commandedAngle;
  lastThinkWriteMs = 0;
  thinkCenterOff = centerOffset;
  thinkAmplitude = amplitude;
  thinkPeriodMs = periodMs == 0 ? 1 : periodMs;
}

void holdPosition(int8_t offsetDeg, uint32_t durationMs) {
  if (!attached) return;
  if (durationMs == 0) durationMs = 1;

  jogStartAngle = commandedAngle;
  jogTargetAngle = offsetToAngle(offsetDeg);
  jogStartMs = millis();
  lastJogWriteMs = 0;
  jogDurationMs = 250;
  jogActive = true;
  playing = nullptr;

  holdActive = true;
  holdUntilMs = millis() + durationMs;
  holdExpiredEdge = false;
}

bool consumeHoldExpired() {
  if (!holdExpiredEdge) return false;
  holdExpiredEdge = false;
  return true;
}

bool isBusy() { return playing != nullptr || jogActive || holdActive; }

}  // namespace Motion
