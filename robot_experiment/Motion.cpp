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

// ---- Patterns --------------------------------------------------------------

static const Keyframe kWaggleFrames[] = {
  {  90,  60 },
  { 105, 120 },
  {  75, 140 },
  { 105, 140 },
  {  90,  80 },
};
static const Pattern kWaggle = { kWaggleFrames,
                                 sizeof(kWaggleFrames) / sizeof(kWaggleFrames[0]) };

// ---- State -----------------------------------------------------------------

static Servo          servo;
static bool           attached = false;

static const Pattern* playing      = nullptr;
static uint8_t        frameIdx     = 0;
static uint32_t       frameStartMs = 0;
static const Pattern* queued       = nullptr;  // depth-1

// Jog slew. A jog is a software-interpolated move from the current
// commanded angle to a new target over `kJogDurationMs`. The servo holds
// at the target after the slew completes (no return to centre — we leave
// it wherever it was until the next jog or thinking-mode takes over).
static bool     jogActive      = false;
static uint8_t  jogStartAngle  = kCentre;
static uint8_t  jogTargetAngle = kCentre;
static uint32_t jogStartMs     = 0;
static uint32_t lastJogWriteMs = 0;
static constexpr uint32_t kJogDurationMs = 250;
static constexpr uint32_t kJogWriteEvery = 20;   // ~50 Hz during slew

// Thinking oscillation.
static bool     thinkingMode     = false;
static uint32_t lastThinkWriteMs = 0;
static uint32_t thinkStartMs     = 0;
static uint8_t  thinkStartAngle  = kCentre;   // where we were when thinking began
static constexpr uint32_t kThinkPeriodMs   = 2000;
static constexpr uint8_t  kThinkAmplitude  = 5;
static constexpr uint32_t kThinkWriteEvery = 20;
static constexpr uint32_t kThinkEaseInMs   = 1000;

// Most recent angle written to the servo. Kept so a new jog can interpolate
// from the actual current position instead of always starting at centre.
static uint8_t commandedAngle = kCentre;

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

void tick() {
  if (!attached) return;

  // 1. Jog slew takes priority — it's the freshest input.
  if (jogActive) {
    const uint32_t now     = millis();
    const uint32_t elapsed = now - jogStartMs;
    if (elapsed >= kJogDurationMs) {
      writeAngle(jogTargetAngle);
      jogActive = false;
    } else if (now - lastJogWriteMs >= kJogWriteEvery) {
      lastJogWriteMs = now;
      const float t      = (float)elapsed / (float)kJogDurationMs;
      const float eased  = t * t * (3.0f - 2.0f * t);       // smoothstep
      const int   delta  = (int)jogTargetAngle - (int)jogStartAngle;
      const int   angle  = (int)jogStartAngle + (int)((float)delta * eased);
      writeAngle((uint8_t)angle);
    }
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

  // 3. Start a queued pattern if one is waiting.
  if (queued) {
    startPattern(queued);
    queued = nullptr;
    return;
  }

  // 4. Nothing else happening — drive thinking oscillation if enabled.
  // Throttled so we don't flood the servo PWM with identical writes.
  if (thinkingMode) {
    const uint32_t now = millis();
    if (now - lastThinkWriteMs >= kThinkWriteEvery) {
      lastThinkWriteMs = now;

      // Ease base angle from wherever the last jog left us back to centre
      // over the first second, and ramp oscillation amplitude in over the
      // same window so the transition is smooth.
      const uint32_t sinceStart = now - thinkStartMs;
      float ease = 1.0f;
      if (sinceStart < kThinkEaseInMs) {
        const float t = (float)sinceStart / (float)kThinkEaseInMs;
        ease = t * t * (3.0f - 2.0f * t);                   // smoothstep
      }
      const float base = (float)thinkStartAngle +
                         ((float)kCentre - (float)thinkStartAngle) * ease;
      const float phase = (float)(now % kThinkPeriodMs) / (float)kThinkPeriodMs;
      const float delta = (float)kThinkAmplitude * ease *
                          sinf(phase * 2.0f * (float)PI);
      writeAngle((uint8_t)(base + delta));
    }
  }
}

void playWaggle() {
  if (!playing && !jogActive) startPattern(&kWaggle);
  else                        queued = &kWaggle;
}

void playJog(int8_t offsetDeg) {
  if (!attached) return;
  int target = (int)kCentre + (int)offsetDeg;
  if (target < 0)   target = 0;
  if (target > 180) target = 180;

  jogStartAngle  = commandedAngle;        // slew from wherever we are now
  jogTargetAngle = (uint8_t)target;
  jogStartMs     = millis();
  lastJogWriteMs = 0;
  jogActive      = true;

  // Jogs preempt pattern playback — they're timely tool-use reactions.
  playing = nullptr;
  queued  = nullptr;
}

void setThinkingMode(bool on) {
  if (on == thinkingMode) return;
  thinkingMode = on;
  if (on) {
    thinkStartMs    = millis();
    thinkStartAngle = commandedAngle;
    lastThinkWriteMs = 0;
  }
}

bool isBusy() { return playing != nullptr || jogActive; }

}  // namespace Motion
