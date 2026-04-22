#include "Motion.h"

#include <ESP32Servo.h>

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

static Servo         servo;
static bool          attached = false;

static const Pattern* playing      = nullptr;
static uint8_t        frameIdx     = 0;
static uint32_t       frameStartMs = 0;

// Depth-1 queue — patterns are short and the caller rarely stacks them.
static const Pattern* queued = nullptr;

static void startPattern(const Pattern* p) {
  playing      = p;
  frameIdx     = 0;
  frameStartMs = millis();
  servo.write(p->frames[0].angle);
}

// ---- Public API ------------------------------------------------------------

void begin() {
  servo.setPeriodHertz(50);                    // SG92R standard 50 Hz
  attached = servo.attach(SERVO_PIN, 500, 2400);
  if (!attached) {
    LOG_ERR("servo attach failed on pin %d", SERVO_PIN);
    return;
  }
  servo.write(90);                             // rest at centre
  LOG_INFO("servo ready on pin %d", SERVO_PIN);
}

void tick() {
  if (!attached) return;

  if (!playing) {
    if (queued) {
      startPattern(queued);
      queued = nullptr;
    }
    return;
  }

  const Keyframe& kf = playing->frames[frameIdx];
  if (millis() - frameStartMs < kf.dwellMs) return;

  frameIdx++;
  if (frameIdx >= playing->count) {
    playing = nullptr;
    return;
  }
  frameStartMs = millis();
  servo.write(playing->frames[frameIdx].angle);
}

void playWaggle() {
  if (!playing) startPattern(&kWaggle);
  else          queued = &kWaggle;
}

bool isBusy() { return playing != nullptr; }

}  // namespace Motion
