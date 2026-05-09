#include "MotionBehaviors.h"

#include <math.h>

#include "../core/DebugLog.h"
#include "../face/FACE_CONFIG.h"
#include "Motion.h"

namespace MotionBehaviors {

static constexpr int8_t kSafeMin = -45;
static constexpr int8_t kSafeMax = 45;

static int16_t sLastExprIdx = -1;
static uint32_t sNextTimedMs = 0;
static bool sOscAtLow = false;
static bool sEmotionArmDriving = false;

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

static int8_t driftPick(const FaceConfig::ExprMotionRow& m) {
  return randInRange((int8_t)((int)m.center - (int)m.amplitude),
                     (int8_t)((int)m.center + (int)m.amplitude));
}

static void onEnter(Face::Expression s) {
  const uint8_t idx = (uint8_t)s;
  if (idx >= (uint8_t)Face::Expression::Count) return;
  const FaceConfig::ExprMotionRow& m = FaceConfig::kMotion[idx];
  const uint32_t now = millis();

  if (m.mode != FaceConfig::MotionMode::Thinking) Motion::setThinkingMode(false);

  switch (m.mode) {
    case FaceConfig::MotionMode::None:
      Motion::cancelAll();
      sNextTimedMs = 0;
      break;

    case FaceConfig::MotionMode::Static:
      Motion::playJog(m.center,
                      m.slew_ms ? m.slew_ms : FaceConfig::kMotionRuntime.default_static_slew_ms);
      sNextTimedMs = 0;
      break;

    case FaceConfig::MotionMode::RandomDrift:
      Motion::playJog(driftPick(m),
                      m.slew_ms ? m.slew_ms : FaceConfig::kMotionRuntime.default_drift_slew_ms);
      sNextTimedMs = now + randRange(m.period_ms, (uint32_t)m.period_ms + m.period_jitter_ms);
      break;

    case FaceConfig::MotionMode::Oscillate:
      sOscAtLow = true;
      {
        const uint16_t halfMs = (uint16_t)(m.period_ms / 2);
        const uint16_t slew = m.slew_ms ? m.slew_ms : halfMs;
        Motion::playJog((int8_t)((int)m.center - (int)m.amplitude), slew);
        sNextTimedMs = now + halfMs;
      }
      break;

    case FaceConfig::MotionMode::Waggle:
      Motion::playWaggle(m.center, m.amplitude, m.period_ms);
      sNextTimedMs = now + m.period_ms;
      break;

    case FaceConfig::MotionMode::Thinking:
      Motion::setThinkingMode(true, m.center, m.amplitude, m.period_ms);
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
  const FaceConfig::ExprMotionRow& m = FaceConfig::kMotion[idx];

  switch (m.mode) {
    case FaceConfig::MotionMode::RandomDrift:
      Motion::playJog(driftPick(m),
                      m.slew_ms ? m.slew_ms : FaceConfig::kMotionRuntime.default_drift_slew_ms);
      sNextTimedMs = now + randRange(m.period_ms, (uint32_t)m.period_ms + m.period_jitter_ms);
      break;

    case FaceConfig::MotionMode::Oscillate:
      sOscAtLow = !sOscAtLow;
      {
        const int8_t off = sOscAtLow ? (int8_t)((int)m.center - (int)m.amplitude)
                                     : (int8_t)((int)m.center + (int)m.amplitude);
        const uint16_t halfMs = (uint16_t)(m.period_ms / 2);
        const uint16_t slew = m.slew_ms ? m.slew_ms : halfMs;
        Motion::playJog(off, slew);
        sNextTimedMs = now + halfMs;
      }
      break;

    case FaceConfig::MotionMode::Waggle:
      Motion::playWaggle(m.center, m.amplitude, m.period_ms);
      sNextTimedMs = now + m.period_ms;
      break;

    case FaceConfig::MotionMode::None:
    case FaceConfig::MotionMode::Static:
    case FaceConfig::MotionMode::Thinking:
      sNextTimedMs = 0;
      break;
  }
}

uint16_t periodMsFor(Face::Expression s) {
  const uint8_t idx = (uint8_t)s;
  if (idx >= (uint8_t)Face::Expression::Count) return 0;
  const FaceConfig::ExprMotionRow& m = FaceConfig::kMotion[idx];
  switch (m.mode) {
    case FaceConfig::MotionMode::Oscillate:
    case FaceConfig::MotionMode::Waggle:
    case FaceConfig::MotionMode::Thinking:
      return m.period_ms;
    case FaceConfig::MotionMode::None:
    case FaceConfig::MotionMode::Static:
    case FaceConfig::MotionMode::RandomDrift:
    default:
      return 0;
  }
}

uint16_t periodMsForContext(const Face::SceneContext& ctx) {
  if (Face::isEmotionExpression(ctx.effective_expression)) {
    const float total =
        ctx.base_emotion_arm.waggle_period_s + ctx.base_emotion_arm.waggle_interval_s;
    float msf = total * 1000.0f;
    if (msf < 50.0f) msf = 50.0f;
    if (msf > 65535.0f) return 65535;
    return (uint16_t)lroundf(msf);
  }
  return periodMsFor(ctx.effective_expression);
}

void begin() {
  Motion::setSafeRange(kSafeMin, kSafeMax);
  sLastExprIdx = -1;
  sNextTimedMs = 0;
  sEmotionArmDriving = false;
}

void tick(const Face::SceneContext& ctx) {
  const Face::Expression expression = ctx.effective_expression;
  const int16_t idx = (int16_t)(uint8_t)expression;
  if (idx < 0 || idx >= (int16_t)Face::Expression::Count) return;

  if (Face::isEmotionExpression(expression)) {
    (void)Motion::consumeHoldExpired();
    if (!sEmotionArmDriving) {
      Motion::resetEmotionArmPhase();
    }
    sEmotionArmDriving = true;
    const Face::EmotionArmMotion& ar = ctx.base_emotion_arm;
    Motion::syncEmotionArmLayer(true, ar.min_offset_deg, ar.max_offset_deg, ar.waggle_period_s,
                                ar.waggle_interval_s);
    sLastExprIdx = idx;
    return;
  }

  sEmotionArmDriving = false;
  Motion::syncEmotionArmLayer(false, 0, 0, 1.0f, 0.0f);

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
