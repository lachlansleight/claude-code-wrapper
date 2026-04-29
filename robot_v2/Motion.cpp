#include "Motion.h"

#include <ESP32Servo.h>
#include <math.h>

#include "DebugLog.h"
#include "config.h"

namespace Motion {

// A keyframe writes an angle and holds for `dwellMs` before the scheduler
// advances to the next one. The servo itself handles slewing — we just
// command positions at rhythm.
struct Keyframe {
  uint8_t  angle;
  uint16_t dwellMs;
};

struct Pattern {
  const Keyframe* frames;
  uint8_t         count;
};

static constexpr uint8_t kCentre = 90;

// ---- State -----------------------------------------------------------------

static Servo          servo;
static bool           attached = false;

// Safe range, in signed degrees relative to kCentre. Wide-open until
// MotionBehaviors::begin() pulls it in via setSafeRange().
static int8_t safeMin = -90;
static int8_t safeMax =  90;

// Waggle buffer. playWaggle() rebuilds these 5 frames from the
// (center, amplitude, period) it's called with so the pattern is fully
// parameterized — we don't keep a hardcoded waggle anywhere.
static Keyframe waggleFrames[5];
static Pattern  waggle = { waggleFrames, 5 };

static const Pattern* playing      = nullptr;
static uint8_t        frameIdx     = 0;
static uint32_t       frameStartMs = 0;

// Jog slew. A jog is a software-interpolated move from the current
// commanded angle to a new target over `jogDurationMs`. The servo holds
// at the target after the slew completes (no return to centre — we leave
// it wherever it was until the next jog or thinking-mode takes over).
static bool     jogActive       = false;
static uint8_t  jogStartAngle   = kCentre;
static uint8_t  jogTargetAngle  = kCentre;
static uint32_t jogStartMs      = 0;
static uint32_t lastJogWriteMs  = 0;
static uint32_t jogDurationMs   = 250;
static constexpr uint32_t kJogWriteEvery = 20;    // ~50 Hz during slew

// Thinking oscillation. Parameters are reset every time the mode is
// (re)enabled by setThinkingMode().
static bool     thinkingMode     = false;
static uint32_t lastThinkWriteMs = 0;
static uint32_t thinkStartMs     = 0;
static uint8_t  thinkStartAngle  = kCentre;
static int8_t   thinkCenterOff   = 0;
static uint8_t  thinkAmplitude   = 5;
static uint16_t thinkPeriodMs    = 2000;
static constexpr uint32_t kThinkWriteEvery = 20;
static constexpr uint32_t kThinkEaseInMs   = 1000;

// Hold override (debug / set_servo_position). While active, the servo
// is locked to a target angle and patterns / thinking-mode are skipped.
// Jog slew is allowed to complete (we use it to glide into the held
// pose). When `holdUntilMs` is reached, the hold ends and
// `holdExpiredEdge` latches true once for MotionBehaviors to consume.
static bool     holdActive       = false;
static uint32_t holdUntilMs      = 0;
static bool     holdExpiredEdge  = false;

// Most recent angle written to the servo. Kept so a new jog can interpolate
// from the actual current position instead of always starting at centre.
static uint8_t commandedAngle = kCentre;

// ---- Helpers --------------------------------------------------------------

static int8_t clampToSafe(int v) {
  if (v < (int)safeMin) v = safeMin;
  if (v > (int)safeMax) v = safeMax;
  return (int8_t)v;
}

static uint8_t offsetToAngle(int8_t offsetDeg) {
  const int8_t safe = clampToSafe(offsetDeg);
  int target = (int)kCentre + (int)safe;
  if (target < 0)   target = 0;
  if (target > 180) target = 180;
  return (uint8_t)target;
}

static void writeAngle(uint8_t a) {
  commandedAngle = a;
  servo.write(a);
}

static void startPattern(const Pattern* p) {
  playing      = p;
  frameIdx     = 0;
  frameStartMs = millis();
  writeAngle(p->frames[0].angle);
}

static void tickJog() {
  const uint32_t now     = millis();
  const uint32_t elapsed = now - jogStartMs;
  if (elapsed >= jogDurationMs) {
    writeAngle(jogTargetAngle);
    jogActive = false;
  } else if (now - lastJogWriteMs >= kJogWriteEvery) {
    lastJogWriteMs = now;
    const float t      = (float)elapsed / (float)jogDurationMs;
    const float eased  = t * t * (3.0f - 2.0f * t);       // smoothstep
    const int   delta  = (int)jogTargetAngle - (int)jogStartAngle;
    const int   angle  = (int)jogStartAngle + (int)((float)delta * eased);
    writeAngle((uint8_t)angle);
  }
}

// ---- Public API ------------------------------------------------------------

void begin() {
  servo.setPeriodHertz(50);                    // SG92R standard 50 Hz
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
  if (maxOffsetDeg >  90) maxOffsetDeg =  90;
  safeMin = minOffsetDeg;
  safeMax = maxOffsetDeg;
  LOG_INFO("servo safe range = [%d, %d]", (int)safeMin, (int)safeMax);
}

void tick() {
  if (!attached) return;

  // 0. Hold override (set_servo_position debug). Locks out everything
  // else for the duration. We still let the jog slew complete so the
  // arm glides smoothly into the held pose.
  if (holdActive) {
    if ((int32_t)(millis() - holdUntilMs) >= 0) {
      holdActive      = false;
      holdExpiredEdge = true;
      // fall through so behaviour resumes immediately
    } else {
      if (jogActive) tickJog();
      return;
    }
  }

  // 1. Jog slew takes priority — it's the freshest input.
  if (jogActive) {
    tickJog();
    return;
  }

  // 2. Advance any currently-playing pattern.
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

  // 3. Nothing else happening — drive thinking oscillation if enabled.
  // Throttled so we don't flood the servo PWM with identical writes.
  if (thinkingMode) {
    const uint32_t now = millis();
    if (now - lastThinkWriteMs >= kThinkWriteEvery) {
      lastThinkWriteMs = now;

      // Ease base angle from wherever the last jog left us toward the
      // configured centre over the first second, and ramp oscillation
      // amplitude in over the same window so the transition is smooth.
      const uint32_t sinceStart = now - thinkStartMs;
      float ease = 1.0f;
      if (sinceStart < kThinkEaseInMs) {
        const float t = (float)sinceStart / (float)kThinkEaseInMs;
        ease = t * t * (3.0f - 2.0f * t);
      }
      const float targetCentre = (float)offsetToAngle(thinkCenterOff);
      const float base  = (float)thinkStartAngle +
                          (targetCentre - (float)thinkStartAngle) * ease;
      const uint16_t period = thinkPeriodMs == 0 ? 1 : thinkPeriodMs;
      const float phase = (float)(now % period) / (float)period;
      const float delta = (float)thinkAmplitude * ease *
                          sinf(phase * 2.0f * (float)PI);
      writeAngle(offsetToAngle((int8_t)((base + delta) - (float)kCentre)));
    }
  }
}

void playWaggle(int8_t center, uint8_t amplitude, uint16_t periodMs) {
  if (!attached) return;
  if (playing || jogActive) return;   // don't preempt; retrigger will catch up

  // Build C → C+A → C-A → C+A → C frames. Frame dwell = period/10 so the
  // full waggle takes period/2 and leaves an equal pause before the
  // caller retriggers.
  if (periodMs < 50) periodMs = 50;
  const uint16_t dwell = (uint16_t)(periodMs / 10);
  const uint8_t  cAng  = offsetToAngle(center);
  const uint8_t  hiAng = offsetToAngle((int8_t)((int)center + (int)amplitude));
  const uint8_t  loAng = offsetToAngle((int8_t)((int)center - (int)amplitude));
  waggleFrames[0] = { cAng,  dwell };
  waggleFrames[1] = { hiAng, dwell };
  waggleFrames[2] = { loAng, dwell };
  waggleFrames[3] = { hiAng, dwell };
  waggleFrames[4] = { cAng,  dwell };
  startPattern(&waggle);
}

void cancelAll() {
  playing      = nullptr;
  jogActive    = false;
  thinkingMode = false;
  // hold/holdExpired are intentionally left alone — the debug hold
  // shouldn't be cancelled by a personality-state change.
}

void playJog(int8_t offsetDeg, uint16_t durationMs) {
  if (!attached) return;
  jogStartAngle  = commandedAngle;
  jogTargetAngle = offsetToAngle(offsetDeg);
  jogStartMs     = millis();
  lastJogWriteMs = 0;
  jogDurationMs  = durationMs > 0 ? durationMs : 1;
  jogActive      = true;

  // Jogs preempt pattern playback — they're timely tool-use reactions.
  playing = nullptr;
}

void setThinkingMode(bool on, int8_t centerOffset, uint8_t amplitude,
                     uint16_t periodMs) {
  if (!on) {
    thinkingMode = false;
    return;
  }
  // Always re-arm the ease-in when (re)enabling, so changing parameters
  // mid-state still produces a smooth transition rather than a snap.
  thinkingMode     = true;
  thinkStartMs     = millis();
  thinkStartAngle  = commandedAngle;
  lastThinkWriteMs = 0;
  thinkCenterOff   = centerOffset;
  thinkAmplitude   = amplitude;
  thinkPeriodMs    = periodMs == 0 ? 1 : periodMs;
}

void holdPosition(int8_t offsetDeg, uint32_t durationMs) {
  if (!attached) return;
  if (durationMs == 0) durationMs = 1;

  jogStartAngle  = commandedAngle;
  jogTargetAngle = offsetToAngle(offsetDeg);
  jogStartMs     = millis();
  lastJogWriteMs = 0;
  jogDurationMs  = 250;
  jogActive      = true;

  playing = nullptr;

  holdActive      = true;
  holdUntilMs     = millis() + durationMs;
  holdExpiredEdge = false;
}

bool consumeHoldExpired() {
  if (!holdExpiredEdge) return false;
  holdExpiredEdge = false;
  return true;
}

bool isBusy() { return playing != nullptr || jogActive || holdActive; }

}  // namespace Motion
