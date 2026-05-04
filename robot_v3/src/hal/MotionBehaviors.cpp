#include "MotionBehaviors.h"

#include "../core/DebugLog.h"
#include "Motion.h"

namespace MotionBehaviors {

static constexpr int8_t kSafeMin = -45;
static constexpr int8_t kSafeMax = 45;

static constexpr uint16_t kDefaultStaticSlewMs = 250;
static constexpr uint16_t kDefaultDriftSlewMs = 500;

enum MotionMode : uint8_t {
  NONE = 0,
  STATIC,
  RANDOM_DRIFT,
  OSCILLATE,
  WAGGLE,
  THINKING,
};

struct ExprMotion {
  MotionMode mode;
  int8_t center;
  uint8_t amplitude;
  uint16_t periodMs;
  uint16_t periodJitterMs;
  uint16_t slewMs;
};

// Rows follow Face::Expression enum order (0 .. Count-1).
static const ExprMotion kMotion[(uint8_t)Face::Expression::Count] = {
    /* Neutral */ {RANDOM_DRIFT, -20, 5, 5000, 5000, 500},
    /* Happy */ {RANDOM_DRIFT, -15, 8, 2000, 1000, 500},
    /* Excited */ {OSCILLATE, -10, 5, 1000, 0, 0},
    /* Joyful */ {WAGGLE, 0, 15, 900, 0, 0},
    /* Sad */ {NONE, 0, 0, 0, 0, 0},
    /* VerbThinking */ {THINKING, -15, 5, 2000, 0, 0},
    /* VerbReading */ {STATIC, -8, 0, 0, 0, 0},
    /* VerbWriting */ {OSCILLATE, 5, 4, 840, 0, 250},
    /* VerbExecuting */ {OSCILLATE, -5, 5, 1000, 0, 0},
    /* VerbStraining */ {OSCILLATE, 0, 5, 750, 0, 0},
    /* VerbSleeping */ {OSCILLATE, -20, 5, 8000, 0, 0},
    /* OverlayWaking */ {STATIC, 18, 0, 0, 0, 0},
    /* OverlayAttention */ {WAGGLE, 0, 15, 900, 0, 0},
};

static_assert(sizeof(kMotion) / sizeof(kMotion[0]) == (uint8_t)Face::Expression::Count,
              "kMotion rows must match Face::Expression::Count");

static int16_t sLastExprIdx = -1;
static uint32_t sNextTimedMs = 0;
static bool sOscAtLow = false;

static int8_t randInRange(int8_t lo, int8_t hi) {
  if (lo > hi) {
    const int8_t t = lo;
    lo = hi;
    hi = t;
  }
  return (int8_t)random((long)lo, (long)hi + 1);
}

static uint32_t randRange(uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;
  return lo + (uint32_t)random((long)(hi - lo + 1));
}

static int8_t driftPick(const ExprMotion& m) {
  return randInRange((int8_t)((int)m.center - (int)m.amplitude),
                     (int8_t)((int)m.center + (int)m.amplitude));
}

static void onEnter(Face::Expression s) {
  const uint8_t idx = (uint8_t)s;
  if (idx >= (uint8_t)Face::Expression::Count) return;
  const ExprMotion& m = kMotion[idx];
  const uint32_t now = millis();

  if (m.mode != THINKING) Motion::setThinkingMode(false);

  switch (m.mode) {
    case NONE:
      Motion::cancelAll();
      sNextTimedMs = 0;
      break;

    case STATIC:
      Motion::playJog(m.center, m.slewMs ? m.slewMs : kDefaultStaticSlewMs);
      sNextTimedMs = 0;
      break;

    case RANDOM_DRIFT:
      Motion::playJog(driftPick(m), m.slewMs ? m.slewMs : kDefaultDriftSlewMs);
      sNextTimedMs = now + randRange(m.periodMs, (uint32_t)m.periodMs + m.periodJitterMs);
      break;

    case OSCILLATE:
      sOscAtLow = true;
      {
        const uint16_t halfMs = (uint16_t)(m.periodMs / 2);
        const uint16_t slew = m.slewMs ? m.slewMs : halfMs;
        Motion::playJog((int8_t)((int)m.center - (int)m.amplitude), slew);
        sNextTimedMs = now + halfMs;
      }
      break;

    case WAGGLE:
      Motion::playWaggle(m.center, m.amplitude, m.periodMs);
      sNextTimedMs = now + m.periodMs;
      break;

    case THINKING:
      Motion::setThinkingMode(true, m.center, m.amplitude, m.periodMs);
      sNextTimedMs = 0;
      break;
  }
}

static void onDuring(Face::Expression s) {
  if (sNextTimedMs == 0) return;
  const uint32_t now = millis();
  if (now < sNextTimedMs) return;

  const uint8_t idx = (uint8_t)s;
  if (idx >= (uint8_t)Face::Expression::Count) return;
  const ExprMotion& m = kMotion[idx];

  switch (m.mode) {
    case RANDOM_DRIFT:
      Motion::playJog(driftPick(m), m.slewMs ? m.slewMs : kDefaultDriftSlewMs);
      sNextTimedMs = now + randRange(m.periodMs, (uint32_t)m.periodMs + m.periodJitterMs);
      break;

    case OSCILLATE:
      sOscAtLow = !sOscAtLow;
      {
        const int8_t off = sOscAtLow ? (int8_t)((int)m.center - (int)m.amplitude)
                                     : (int8_t)((int)m.center + (int)m.amplitude);
        const uint16_t halfMs = (uint16_t)(m.periodMs / 2);
        const uint16_t slew = m.slewMs ? m.slewMs : halfMs;
        Motion::playJog(off, slew);
        sNextTimedMs = now + halfMs;
      }
      break;

    case WAGGLE:
      Motion::playWaggle(m.center, m.amplitude, m.periodMs);
      sNextTimedMs = now + m.periodMs;
      break;

    case NONE:
    case STATIC:
    case THINKING:
      sNextTimedMs = 0;
      break;
  }
}

uint16_t periodMsFor(Face::Expression s) {
  const uint8_t idx = (uint8_t)s;
  if (idx >= (uint8_t)Face::Expression::Count) return 0;
  const ExprMotion& m = kMotion[idx];
  switch (m.mode) {
    case OSCILLATE:
    case WAGGLE:
    case THINKING:
      return m.periodMs;
    case NONE:
    case STATIC:
    case RANDOM_DRIFT:
    default:
      return 0;
  }
}

void begin() {
  Motion::setSafeRange(kSafeMin, kSafeMax);
  sLastExprIdx = -1;
  sNextTimedMs = 0;
}

void tick(Face::Expression expression) {
  const int16_t idx = (int16_t)(uint8_t)expression;
  if (idx < 0 || idx >= (int16_t)Face::Expression::Count) return;

  if (idx != sLastExprIdx) {
    LOG_EVT("motion: enter %s", Face::expressionName(expression));
    sLastExprIdx = idx;
    onEnter(expression);
  } else if (Motion::consumeHoldExpired()) {
    LOG_EVT("motion: hold expired, re-entering %s", Face::expressionName(expression));
    onEnter(expression);
  }
  onDuring(expression);
}

}  // namespace MotionBehaviors
