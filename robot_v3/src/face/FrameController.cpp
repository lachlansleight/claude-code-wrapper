#include "FrameController.h"

#include <esp_random.h>
#include <math.h>

#include "../hal/Display.h"
#include "../hal/MotionBehaviors.h"
#include "../hal/Settings.h"
#include "Scene.h"
#include "TextScene.h"

namespace Face {

static constexpr float kMoodRingTauMs = 200.0f;

static FaceParams targetForExpression(Expression s, const FaceParams* baseTargets) {
  const uint8_t idx = (uint8_t)s;
  if (idx >= (uint8_t)Expression::Count) return baseTargets[0];
  return baseTargets[idx];
}

// Resolve the live tween target. For emotion expressions the base
// FaceParams (including ring_*) come from the continuous (v,a) blend in
// SceneContextFill; for verbs and overlays it's the static kBaseTargets row.
static FaceParams targetForContext(const SceneContext& ctx, const FaceParams* baseTargets) {
  if (Face::isEmotionExpression(ctx.effective_expression)) {
    return ctx.base_face_params;
  }
  return targetForExpression(ctx.effective_expression, baseTargets);
}

// Row order matches Face::Expression (see SceneTypes.h).
// Field order matches FaceParams declaration:
//   eye_dy, eye_rx,
//   eye_top_apex, eye_top_corner, eye_bot_apex, eye_bot_corner, eye_thick,
//   eye_wave_amp, eye_wave_freq, eye_wave_speed,
//   pupil_dx, pupil_dy, pupil_r,
//   mouth_dy, mouth_rx,
//   mouth_top_apex, mouth_top_corner, mouth_bot_apex, mouth_bot_corner, mouth_thick,
//   mouth_wave_amp, mouth_wave_freq, mouth_wave_speed,
//   face_rot, face_y,
//   ring_r, ring_g, ring_b
static const FaceParams kBaseTargets[(uint8_t)Expression::Count] = {
    /* Neutral */          {  2, 30,  -26, 0, +26, 0, 3,  0, 0, 0,   0,  3, 15,
                              0, 15,   +2, 0,  +2, 0, 3,  0, 0, 0,
                              0, 0,    0, 0, 0 },
    /* Happy */            {  0, 30,  -16, 0, +30, 0, 3,  0, 0, 0,   0,  5,  16,
                              0,  +24,   3, 0,  3, 0, 3,  0, 0, 0,
                              0, 5,    0, 0, 0 },
    /* Excited */          {  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   0,  0,  17,
                              0,  +27,   4, -2,  8, -2, 3,  0, 0, 0,
                              0, 0,    40, 255, 80 },
    /* Joyful */           { -5, 20,  -15, 0, -6, 0, 4,  0, 0, 0,   0,  0,  14,
                              -11,  +37,   3, 0,  24, 0, 4,  0, 0, 0,
                              0, -14,    255, 228, 38 },
    /* Sad */              {  4, 28,  -12, 0, +17, 0, 3,  0, 0, 0,   0,  3,  11,
                              4,  +20,   -13, -7,  -11, -8, 3,  0, 0, 0,
                              0, 6,    0, 0, 0 },
    /* VerbThinking */     {  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   7, -9, 15,
                              0, 11,   +3, 0,  +3, 0, 3,  0, 0, 0,
                            -10, 0,    36, 56, 120 },
    /* VerbReading */      {  0, 28,  -26, 0, +26, 0, 3,  0, 0, 0,   0,  8, 12,
                              0,  9,   +3, 0,  +3, 0, 3,  0, 0, 0,
                              0, 12,   78, 146, 210 },
    /* VerbWriting */      {  0, 30,  -26, 0, +26, 0, 3,  0, 0, 0,   0, -8, 15,
                              0, 15,    0, 0, +14, 0, 3,  0, 0, 0,
                              0, 0,    104, 118, 228 },
    /* VerbExecuting */    {  0, 30,  -16, 0, +16, 0, 3,  0, 0, 0,   0, -4, 10,
                              0,  9,   +2, 0,  +2, 0, 3,  0, 0, 0,
                              0, 0,    156, 64, 216 },
    /* VerbStraining */    {  0, 30,  -22, 0, +22, 0, 3,  0, 0, 0,   0, -3, 10,
                              0, 18,    0, 0,   0, 0, 3,  4, 100, 360,
                              0, 0,    210, 75, 220 },
    /* VerbSleeping */     {  8, 26,   -2, 0,  +2, 0, 3,  0, 0, 0,   0,  0,  15,
                              0,  9,    0, 0,   0, 0, 3,  0, 0, 0,
                              0, 0,    0, 0, 0 },
    /* OverlayWaking */    { -2, 34,  -34, 0, +34, 0, 3,  0, 0, 0,   0,  0, 18,
                              0,  7,   -9, 0,  +9, 0, 3,  0, 0, 0,
                              0, 0,    128, 128, 128 },
    /* OverlayAttention */ { -2, 34,  -34, 0, +34, 0, 3,  0, 0, 0,   0,  0, 18,
                              0,  7,   -9, 0,  +9, 0, 3,  0, 0, 0,
                              0, 0,    255, 20, 40 },
    /* Sleepy */           {  0, 28,  0, 10, +34, 10, 3,  0, 0, 0,   0,  0,  15,
                              0,  +13,   0, 0,  3, 0, 3,  0, 17, 90,
                              0, 9,    0, 0, 0 },
    /* Distressed */       {  2, 30,  -26, 0, +33, 0, 3,  0, 0, 0,   0,  7,  10,
                              4,  +24,   -19, -7,  -7, 0, 3,  0, 0, 0,
                              0, -15,    255, 48, 24 },
    /* Blissed */          {  1, 20,   +3, 0, +1, 0, 3,  0, 0, 0,   0,  0,  15,
                              1,  +26,   3, 0,  13, 0, 3,  0, 0, 0,
                              0, 5,    0, 0, 0 },
    /* Depressed */        {  0, 30,   +16, 10, +34, 11, 3,  0, 0, 0,   0,  20,  6,
                              0,  +13,   0, +6,  3, 4, 3,  0, 17, 90,
                              0, 9,    0, 0, 0 },
    /* Shocked */           { 0, 30,   -34, 0,   39, 0, 3,   1, 85, 720,   0, 3, 9,
                             20, 17,  -17, 0,   8, 0, 1,    2, 49, 720,   
                             0, 0,     255, 255, 255 },
    /* Disappointed */      {  2, 30,   +6, 0, +6, 0, 3,  0, 0, 0,   0,  3,  15,
                              4,  +13,   -8, 0,  -8, 0, 3,  0, 0, 0,
                              0, 0,    229, 54, 95 },
    /* Cheeky */            {  1, 30,  -31, 0, +8, 0, 3,  0, 0, 0,   0,  3,  15,
                              -25,  +15,   11, 0,  8, 0, 3,  0, 0, 0,
                              0, -3,    0, 0, 0 },
    /* Gleeful */           {  1, 27,  -30, 0, -2, 0, 3,  0, 0, 0,   0,  -7,  10,
                              -25,  +27,   0, -2,  20, -2, 3,  0, 0, 0,
                              0, 5,    39, 248, 78 },

};

static constexpr uint32_t kTweenMs = 250;
static constexpr uint32_t kTickIntervalMs = 33;
static constexpr uint32_t kTickIntervalStreamMs = 16;

static constexpr uint32_t kBlinkCloseMs = 80;
static constexpr uint32_t kBlinkOpenMs = 130;

static constexpr uint32_t kThinkingFlipDurMs = 600;
static constexpr uint32_t kThinkingFlipMinMs = 3000;
static constexpr uint32_t kThinkingFlipMaxMs = 6000;

static FaceParams sFrom;
static FaceParams sTo;
static uint32_t sTweenStartMs = 0;
static int16_t sLastExprIdx = -1;

static uint32_t sNextBlinkMs = 0;
static uint32_t sBlinkStartMs = 0;
static bool sBlinkActive = false;

static uint32_t sLastTickMs = 0;
static float sMoodR = 0.0f;
static float sMoodG = 0.0f;
static float sMoodB = 0.0f;
static uint32_t sLastMoodMs = 0;
static uint32_t sProgressFadeStartMs = 0;
static uint16_t sFadeReadCount = 0;
static uint16_t sFadeWriteCount = 0;
static float sTextStreamAlpha = 0.0f;
static float sWriteStreamAlpha = 0.0f;
static uint32_t sLastEffectsMs = 0;
static uint32_t sLastSettingsVersion = 0;

static float sThinkFromSign = 1.0f;
static float sThinkToSign = 1.0f;
static uint32_t sThinkFlipStartMs = 0;
static uint32_t sNextThinkFlipMs = 0;

static int16_t sIdleGlanceDx = 0;
static int16_t sIdleGlanceDy = 0;
static int16_t sIdleGlanceFromDx = 0;
static int16_t sIdleGlanceFromDy = 0;
static uint32_t sIdleGlanceStartMs = 0;
static uint32_t sNextIdleGlanceMs = 0;
static constexpr uint32_t kIdleGlanceTweenMs = 200;

// Body vertical bob: integrate phase so a changing waggle period (blended
// emotion) does not re-phase from wall-clock % period (which jitters).
static float sBodyBobPhaseRad = 0.0f;
static uint32_t sBodyBobPhaseLastMs = 0;

static int16_t lerpi(int16_t a, int16_t b, float t) { return (int16_t)(a + (b - a) * t); }

static FaceParams lerpParams(const FaceParams& a, const FaceParams& b, float t) {
  FaceParams r;
  r.eye_dy = lerpi(a.eye_dy, b.eye_dy, t);
  r.eye_rx = lerpi(a.eye_rx, b.eye_rx, t);
  r.eye_top_apex = lerpi(a.eye_top_apex, b.eye_top_apex, t);
  r.eye_top_corner = lerpi(a.eye_top_corner, b.eye_top_corner, t);
  r.eye_bot_apex = lerpi(a.eye_bot_apex, b.eye_bot_apex, t);
  r.eye_bot_corner = lerpi(a.eye_bot_corner, b.eye_bot_corner, t);
  r.eye_thick = lerpi(a.eye_thick, b.eye_thick, t);
  r.eye_wave_amp = lerpi(a.eye_wave_amp, b.eye_wave_amp, t);
  r.eye_wave_freq = lerpi(a.eye_wave_freq, b.eye_wave_freq, t);
  r.eye_wave_speed = lerpi(a.eye_wave_speed, b.eye_wave_speed, t);
  r.pupil_dx = lerpi(a.pupil_dx, b.pupil_dx, t);
  r.pupil_dy = lerpi(a.pupil_dy, b.pupil_dy, t);
  r.pupil_r = lerpi(a.pupil_r, b.pupil_r, t);
  r.mouth_dy = lerpi(a.mouth_dy, b.mouth_dy, t);
  r.mouth_rx = lerpi(a.mouth_rx, b.mouth_rx, t);
  r.mouth_top_apex = lerpi(a.mouth_top_apex, b.mouth_top_apex, t);
  r.mouth_top_corner = lerpi(a.mouth_top_corner, b.mouth_top_corner, t);
  r.mouth_bot_apex = lerpi(a.mouth_bot_apex, b.mouth_bot_apex, t);
  r.mouth_bot_corner = lerpi(a.mouth_bot_corner, b.mouth_bot_corner, t);
  r.mouth_thick = lerpi(a.mouth_thick, b.mouth_thick, t);
  r.mouth_wave_amp = lerpi(a.mouth_wave_amp, b.mouth_wave_amp, t);
  r.mouth_wave_freq = lerpi(a.mouth_wave_freq, b.mouth_wave_freq, t);
  r.mouth_wave_speed = lerpi(a.mouth_wave_speed, b.mouth_wave_speed, t);
  r.face_rot = lerpi(a.face_rot, b.face_rot, t);
  r.face_y = lerpi(a.face_y, b.face_y, t);
  r.ring_r = lerpi(a.ring_r, b.ring_r, t);
  r.ring_g = lerpi(a.ring_g, b.ring_g, t);
  r.ring_b = lerpi(a.ring_b, b.ring_b, t);
  return r;
}

static float breathPhase(uint32_t now) {
  const float t = (float)(now % 4000) / 4000.0f;
  return sinf(t * 2.0f * (float)PI);
}

static int16_t bodyBobFor(const SceneContext& ctx, uint32_t now) {
  const Expression s = ctx.effective_expression;
  const uint16_t period = MotionBehaviors::periodMsForContext(ctx);
  if (period == 0) {
    sBodyBobPhaseLastMs = now;
    return 0;
  }

  int16_t amp = 0;
  bool integrate = false;
  if (Face::isEmotionExpression(s)) {
    integrate = true;
    if (ctx.base_emotion_arm.min_offset_deg != ctx.base_emotion_arm.max_offset_deg) amp = 3;
  } else {
    switch (s) {
      case Expression::VerbSleeping:
        amp = 10;
        break;
      case Expression::VerbExecuting:
      case Expression::VerbStraining:
      case Expression::Excited:
        amp = 5;
        break;
      case Expression::Joyful:
        amp = 7;
        break;
      default:
        amp = 0;
        break;
    }
    integrate = (amp != 0);
  }

  constexpr float kTwoPi = 2.0f * (float)PI;
  if (integrate) {
    const float dt_ms =
        (sBodyBobPhaseLastMs == 0) ? 0.0f : (float)(now - sBodyBobPhaseLastMs);
    sBodyBobPhaseLastMs = now;
    sBodyBobPhaseRad += (kTwoPi / (float)period) * dt_ms;
    sBodyBobPhaseRad = fmodf(sBodyBobPhaseRad, kTwoPi);
    if (sBodyBobPhaseRad < 0.0f) sBodyBobPhaseRad += kTwoPi;
  } else {
    sBodyBobPhaseLastMs = now;
  }

  if (amp == 0) return 0;
  return (int16_t)(-sinf(sBodyBobPhaseRad) * (float)amp);
}

static void gazeFor(Expression s, uint32_t now, int16_t& gdx, int16_t& gdy) {
  gdx = 0;
  gdy = 0;
  switch (s) {
    case Expression::Neutral:
      if (sIdleGlanceStartMs != 0) {
        const float t =
            smoothstep01((float)(now - sIdleGlanceStartMs) / (float)kIdleGlanceTweenMs);
        gdx = lerpi(sIdleGlanceFromDx, sIdleGlanceDx, t);
        gdy = lerpi(sIdleGlanceFromDy, sIdleGlanceDy, t);
      } else {
        gdx = sIdleGlanceDx;
        gdy = sIdleGlanceDy;
      }

      if (sNextIdleGlanceMs == 0 || now >= sNextIdleGlanceMs) {
        sIdleGlanceFromDx = gdx;
        sIdleGlanceFromDy = gdy;
        sIdleGlanceDx = (int16_t)random(-15, 16);
        sIdleGlanceDy = (int16_t)random(-10, 11);
        sIdleGlanceStartMs = now;
        sNextIdleGlanceMs = now + (uint32_t)random(1000, 10001);
      }
      break;
    case Expression::VerbThinking: {
      const float t = (float)(now % 900) / 900.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 2);
      gdy = (int16_t)(cosf(t * 2 * (float)PI) * 2);
      break;
    }
    case Expression::VerbReading: {
      const float t = (float)(now % 1300) / 1300.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 6);
      break;
    }
    case Expression::VerbWriting: {
      const float t = (float)(now % 2200) / 2200.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 2);
      break;
    }
    case Expression::VerbExecuting:
    case Expression::VerbStraining: {
      const float t = (float)(now % 2500) / 2500.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 1);
      break;
    }
    case Expression::Excited: {
      const float t = (float)(now % 3500) / 3500.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 3);
      gdy = (int16_t)(cosf(t * 2 * (float)PI) * 2);
      break;
    }
    case Expression::Happy: {
      const float t = (float)(now % 5500) / 5500.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 2);
      break;
    }
    default:
      break;
  }
}

static uint32_t blinkPeriodMsFor(Expression s) {
  switch (s) {
    case Expression::Neutral:
      return (uint32_t)random(4000, 6500);
    case Expression::VerbThinking:
      return (uint32_t)random(2000, 3500);
    case Expression::VerbReading:
      return (uint32_t)random(4000, 6000);
    case Expression::VerbWriting:
      return (uint32_t)random(3500, 5500);
    case Expression::VerbExecuting:
    case Expression::VerbStraining:
      return (uint32_t)random(4500, 7000);
    case Expression::Excited:
      return (uint32_t)random(2500, 4000);
    case Expression::Happy:
      return (uint32_t)random(3000, 4500);
    case Expression::Sleepy:
      return (uint32_t)random(5000, 8000);
    case Expression::Distressed:
    case Expression::Depressed:
    case Expression::Shocked:
    case Expression::Disappointed:
      return (uint32_t)random(2000, 4000);
    case Expression::Blissed:
      return (uint32_t)random(3500, 5500);
    case Expression::Cheeky:
      return (uint32_t)random(2800, 4200);
    case Expression::Gleeful:
      return (uint32_t)random(2200, 3800);
    default:
      return 0;
  }
}

static void scheduleNextBlink(Expression s, uint32_t from) {
  const uint32_t p = blinkPeriodMsFor(s);
  sNextBlinkMs = (p == 0) ? 0 : from + p;
}

static float currentBlinkAmount(uint32_t now) {
  if (!sBlinkActive) return 0.0f;
  const uint32_t d = now - sBlinkStartMs;
  if (d < kBlinkCloseMs) {
    return (float)d / (float)kBlinkCloseMs;
  }
  const uint32_t d2 = d - kBlinkCloseMs;
  if (d2 < kBlinkOpenMs) {
    return 1.0f - (float)d2 / (float)kBlinkOpenMs;
  }
  sBlinkActive = false;
  return 0.0f;
}

static float currentThinkSign(uint32_t now) {
  if (sThinkFlipStartMs == 0) return sThinkToSign;
  const float t = (float)(now - sThinkFlipStartMs) / (float)kThinkingFlipDurMs;
  return sThinkFromSign + (sThinkToSign - sThinkFromSign) * smoothstep01(t);
}

static void resetThinkTilt(uint32_t now) {
  sThinkFromSign = 1.0f;
  sThinkToSign = 1.0f;
  sThinkFlipStartMs = 0;
  sNextThinkFlipMs =
      now + (uint32_t)random((long)kThinkingFlipMinMs, (long)kThinkingFlipMaxMs + 1);
}

static void maybeFlipThinkTilt(uint32_t now) {
  if (sNextThinkFlipMs == 0 || now < sNextThinkFlipMs) return;
  sThinkFromSign = currentThinkSign(now);
  sThinkToSign = -sThinkFromSign;
  sThinkFlipStartMs = now;
  sNextThinkFlipMs = now + kThinkingFlipDurMs +
                     (uint32_t)random((long)kThinkingFlipMinMs, (long)kThinkingFlipMaxMs + 1);
}

static constexpr uint32_t kProgressFadeMs = 280;
static constexpr uint32_t kEffectsFadeMs = 100;

void begin() {
  randomSeed(esp_random());

  sLastExprIdx = -1;
  sFrom = targetForExpression(Expression::VerbSleeping, kBaseTargets);
  sTo = sFrom;
  sTweenStartMs = millis();
  sNextBlinkMs = 0;
  sBlinkActive = false;
  sLastTickMs = 0;

  sThinkFromSign = 1.0f;
  sThinkToSign = 1.0f;
  sThinkFlipStartMs = 0;
  sNextThinkFlipMs = 0;

  sMoodR = (float)sFrom.ring_r;
  sMoodG = (float)sFrom.ring_g;
  sMoodB = (float)sFrom.ring_b;
  sLastMoodMs = millis();
  sTextStreamAlpha = 0.0f;
  sWriteStreamAlpha = 0.0f;
  sLastEffectsMs = millis();
  sProgressFadeStartMs = 0;
  sFadeReadCount = 0;
  sFadeWriteCount = 0;
  sIdleGlanceDx = 0;
  sIdleGlanceDy = 0;
  sIdleGlanceFromDx = 0;
  sIdleGlanceFromDy = 0;
  sIdleGlanceStartMs = 0;
  sNextIdleGlanceMs = 0;
  sLastSettingsVersion = Settings::settingsVersion();
}

void invalidate() { sLastTickMs = 0; }

const FaceParams& baseTargetFor(Expression e) {
  const uint8_t idx = (uint8_t)e;
  if (idx >= (uint8_t)Expression::Count) return kBaseTargets[0];
  return kBaseTargets[idx];
}

static void onExpressionChange(Expression newExpr, uint32_t now, const SceneContext& ctx) {
  const float t = (float)(now - sTweenStartMs) / (float)kTweenMs;
  FaceParams currentFrame = lerpParams(sFrom, sTo, smoothstep01(t));

  const bool hadOld = (sLastExprIdx >= 0);
  const Expression oldExpr = hadOld ? (Expression)(uint8_t)sLastExprIdx : Expression::VerbSleeping;

  if (hadOld && oldExpr == Expression::VerbThinking && newExpr != Expression::VerbThinking) {
    const float sign = currentThinkSign(now);
    currentFrame.face_rot = (int16_t)((float)currentFrame.face_rot * sign);
    currentFrame.pupil_dx = (int16_t)((float)currentFrame.pupil_dx * sign);
  }

  sFrom = currentFrame;
  sTo = targetForContext(ctx, kBaseTargets);
  sTweenStartMs = now;

  if (hadOld && oldExpr == Expression::Happy && newExpr == Expression::Neutral) {
    sFadeReadCount = ctx.read_tools_this_turn;
    sFadeWriteCount = ctx.write_tools_this_turn;
    sProgressFadeStartMs = now;
  } else {
    sProgressFadeStartMs = 0;
    sFadeReadCount = 0;
    sFadeWriteCount = 0;
  }

  sLastExprIdx = (int16_t)(uint8_t)newExpr;

  sBlinkActive = false;
  scheduleNextBlink(newExpr, now);

  if (newExpr == Expression::VerbThinking) {
    resetThinkTilt(now);
  }

  if (newExpr == Expression::Neutral) {
    sIdleGlanceFromDx = sIdleGlanceDx;
    sIdleGlanceFromDy = sIdleGlanceDy;
    sIdleGlanceStartMs = now;
    sNextIdleGlanceMs = now;
  } else {
    sIdleGlanceDx = 0;
    sIdleGlanceDy = 0;
    sIdleGlanceFromDx = 0;
    sIdleGlanceFromDy = 0;
    sIdleGlanceStartMs = 0;
    sNextIdleGlanceMs = 0;
  }
}

void tick(const SceneContext& ctx) {
  if (!Display::ready()) return;

  const uint32_t now = millis();
  const Expression sNow = ctx.effective_expression;
  const uint8_t exprIdx = (uint8_t)sNow;
  if (exprIdx >= (uint8_t)Expression::Count) return;

  const uint32_t settingsVersion = ctx.settings_version;
  if (settingsVersion != sLastSettingsVersion) {
    sLastSettingsVersion = settingsVersion;
    sTo = targetForContext(ctx, kBaseTargets);
    sFrom.ring_r = sTo.ring_r;
    sFrom.ring_g = sTo.ring_g;
    sFrom.ring_b = sTo.ring_b;
    sMoodR = (float)sTo.ring_r;
    sMoodG = (float)sTo.ring_g;
    sMoodB = (float)sTo.ring_b;
    sLastMoodMs = now;
  }

  const bool streamFrame = (sNow == Expression::VerbReading || sNow == Expression::VerbWriting ||
                            sTextStreamAlpha > 0.02f || sWriteStreamAlpha > 0.02f);
  const uint32_t tickInterval = streamFrame ? kTickIntervalStreamMs : kTickIntervalMs;
  if (now - sLastTickMs < tickInterval) return;
  sLastTickMs = now;

  const int16_t idx = (int16_t)exprIdx;
  if (idx != sLastExprIdx) {
    onExpressionChange(sNow, now, ctx);
  } else if (Face::isEmotionExpression(sNow)) {
    // Continuous emotion blend: refresh the tween target every tick so
    // the face follows the (v, a) point smoothly even without an
    // expression-enum change. sFrom/tween-start are untouched, so any
    // in-flight transition from a verb/overlay still completes
    // normally; once tw >= 1, the rendered params equal sTo (live).
    sTo = targetForContext(ctx, kBaseTargets);
  }

  const float tw = (float)(now - sTweenStartMs) / (float)kTweenMs;
  const float te = smoothstep01(tw);
  FaceParams p = lerpParams(sFrom, sTo, te);

  const uint32_t moodDt = (sLastMoodMs == 0) ? 0 : (now - sLastMoodMs);
  const float alpha = 1.0f - expf(-(float)moodDt / kMoodRingTauMs);
  sMoodR += ((float)p.ring_r - sMoodR) * alpha;
  sMoodG += ((float)p.ring_g - sMoodG) * alpha;
  sMoodB += ((float)p.ring_b - sMoodB) * alpha;
  sLastMoodMs = now;

  const uint32_t effectsDt = (sLastEffectsMs == 0) ? 0 : (now - sLastEffectsMs);
  const float effectsA = 1.0f - expf(-(float)effectsDt / (float)kEffectsFadeMs);
  const float readTarget = (sNow == Expression::VerbReading) ? 1.0f : 0.0f;
  const float writeTarget = (sNow == Expression::VerbWriting) ? 1.0f : 0.0f;
  sTextStreamAlpha += (readTarget - sTextStreamAlpha) * effectsA;
  sWriteStreamAlpha += (writeTarget - sWriteStreamAlpha) * effectsA;
  sLastEffectsMs = now;

  if (sProgressFadeStartMs != 0 && now - sProgressFadeStartMs >= kProgressFadeMs) {
    sProgressFadeStartMs = 0;
    sFadeReadCount = 0;
    sFadeWriteCount = 0;
  }

  if (sNow != Expression::Joyful && sNow != Expression::Gleeful &&
      sNow != Expression::VerbSleeping) {
    const int16_t b = (int16_t)(breathPhase(now) * 1.5f);
    p.eye_dy = (int16_t)(p.eye_dy + b);
    p.mouth_dy = (int16_t)(p.mouth_dy + b / 2);
  }

  p.face_y = (int16_t)(p.face_y + bodyBobFor(ctx, now));

  if (sNow == Expression::VerbThinking) {
    maybeFlipThinkTilt(now);
    const float sign = currentThinkSign(now);
    p.face_rot = (int16_t)((float)p.face_rot * sign);
    p.pupil_dx = (int16_t)((float)p.pupil_dx * sign);
  }

  if (!sBlinkActive) {
    if (sNextBlinkMs == 0) {
      scheduleNextBlink(sNow, now);
    } else if (now >= sNextBlinkMs) {
      sBlinkActive = true;
      sBlinkStartMs = now;
      sNextBlinkMs = 0;
    }
  }
  const float blinkAmt = currentBlinkAmount(now);
  if (!sBlinkActive && sNextBlinkMs == 0) {
    scheduleNextBlink(sNow, now);
  }

  int16_t gdx = 0, gdy = 0;
  gazeFor(sNow, now, gdx, gdy);

  const uint16_t fg565 = rgb888To565(ctx.fg_r, ctx.fg_g, ctx.fg_b);
  const uint16_t bg565 = rgb888To565(ctx.bg_r, ctx.bg_g, ctx.bg_b);
  const uint16_t divider565 = Settings::color565Scaled(Settings::NamedColor::Foreground, 96);

  TFT_eSprite& spr = Display::sprite();
  SceneRenderState renderState = {sNow,
                                  sMoodR,
                                  sMoodG,
                                  sMoodB,
                                  sTextStreamAlpha,
                                  sWriteStreamAlpha,
                                  sProgressFadeStartMs,
                                  sFadeReadCount,
                                  sFadeWriteCount,
                                  fg565,
                                  bg565,
                                  divider565};

  if (ctx.render_mode == (uint8_t)RenderMode::Face) {
    renderScene(spr, p, blinkAmt, gdx, gdy, renderState, ctx, now);
  } else {
    renderTextScene(spr, renderState, ctx, now);
  }
  Display::pushFrame();
}

}  // namespace Face
