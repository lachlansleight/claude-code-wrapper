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

static FaceParams makeFaceParamsWithMood(const FaceParams& base, Settings::NamedColor moodColor) {
  FaceParams p = base;
  const Settings::Rgb888 c = Settings::colorRgb(moodColor);
  p.ring_r = c.r;
  p.ring_g = c.g;
  p.ring_b = c.b;
  return p;
}

static Settings::NamedColor moodColorForExpression(Expression e) {
  switch (e) {
    case Expression::Neutral:
      return Settings::NamedColor::Background;
    case Expression::Happy:
      return Settings::NamedColor::Happy;
    case Expression::Excited:
      return Settings::NamedColor::Excited;
    case Expression::Joyful:
      return Settings::NamedColor::Joyful;
    case Expression::Sad:
      return Settings::NamedColor::Sad;
    case Expression::VerbThinking:
      return Settings::NamedColor::Thinking;
    case Expression::VerbReading:
      return Settings::NamedColor::Reading;
    case Expression::VerbWriting:
      return Settings::NamedColor::Writing;
    case Expression::VerbExecuting:
      return Settings::NamedColor::Executing;
    case Expression::VerbStraining:
      return Settings::NamedColor::Straining;
    case Expression::VerbSleeping:
      return Settings::NamedColor::Sleeping;
    case Expression::OverlayWaking:
      return Settings::NamedColor::Excited;
    case Expression::OverlayAttention:
      return Settings::NamedColor::Attention;
    default:
      return Settings::NamedColor::Background;
  }
}

static FaceParams targetForExpression(Expression s, const FaceParams* baseTargets) {
  const uint8_t idx = (uint8_t)s;
  if (idx >= (uint8_t)Expression::Count) return baseTargets[0];
  return makeFaceParamsWithMood(baseTargets[idx], moodColorForExpression(s));
}

// Row order matches Face::Expression (see SceneTypes.h).
static const FaceParams kBaseTargets[(uint8_t)Expression::Count] = {
    /* Neutral */ {2, 30, 26, 3, 0, 0, 3, 15, 0, 30, -2, 0, 3, 0, 0, 0, 0, 0},
    /* Happy */ {0, 30, 30, 3, 0, 0, 0, 15, 0, 26, -3, 0, 3, 0, 0, 0, 0, 0},
    /* Excited */ {0, 30, 30, 3, 0, 0, 0, 15, 0, 30, -8, 0, 3, 0, 0, 0, 0, 0},
    /* Joyful */ {-4, 24, 4, 4, 7, 0, 0, 0, 0, 36, -1, 14, 4, 0, 0, 0, 0, 0},
    /* Sad */ {2, 30, 22, 3, -6, 0, 3, 15, 4, 26, 8, 0, 3, 0, 0, 0, 0, 0},
    /* VerbThinking */ {0, 30, 30, 3, 0, 7, -9, 15, 0, 22, -3, 0, 3, -10, 0, 0, 0, 0},
    /* VerbReading */ {0, 28, 26, 3, 0, 0, 8, 12, 0, 18, -3, 0, 3, 0, 12, 0, 0, 0},
    /* VerbWriting */ {0, 30, 26, 3, 0, 0, -8, 15, 0, 30, -1, 14, 3, 0, 0, 0, 0, 0},
    /* VerbExecuting */ {0, 30, 16, 3, 0, 0, -4, 10, 0, 18, -2, 0, 3, 0, 0, 0, 0, 0},
    /* VerbStraining */ {0, 30, 22, 3, 0, 0, -3, 10, 0, 18, 0, 0, 3, 0, 0, 0, 0, 0},
    /* VerbSleeping */ {8, 26, 2, 3, 0, 0, 0, 0, 0, 18, 0, 0, 3, 0, 0, 0, 0, 0},
    /* OverlayWaking */ {-2, 34, 34, 3, 0, 0, 0, 18, 0, 14, 0, 9, 3, 0, 0, 0, 0, 0},
    /* OverlayAttention */ {-2, 34, 34, 3, 0, 0, 0, 18, 0, 14, 0, 9, 3, 0, 0, 0, 0, 0},
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

static int16_t lerpi(int16_t a, int16_t b, float t) { return (int16_t)(a + (b - a) * t); }

static FaceParams lerpParams(const FaceParams& a, const FaceParams& b, float t) {
  FaceParams r;
  r.eye_dy = lerpi(a.eye_dy, b.eye_dy, t);
  r.eye_rx = lerpi(a.eye_rx, b.eye_rx, t);
  r.eye_ry = lerpi(a.eye_ry, b.eye_ry, t);
  r.eye_stroke = lerpi(a.eye_stroke, b.eye_stroke, t);
  r.eye_curve = lerpi(a.eye_curve, b.eye_curve, t);
  r.pupil_dx = lerpi(a.pupil_dx, b.pupil_dx, t);
  r.pupil_dy = lerpi(a.pupil_dy, b.pupil_dy, t);
  r.pupil_r = lerpi(a.pupil_r, b.pupil_r, t);
  r.mouth_dy = lerpi(a.mouth_dy, b.mouth_dy, t);
  r.mouth_w = lerpi(a.mouth_w, b.mouth_w, t);
  r.mouth_curve = lerpi(a.mouth_curve, b.mouth_curve, t);
  r.mouth_open_h = lerpi(a.mouth_open_h, b.mouth_open_h, t);
  r.mouth_thick = lerpi(a.mouth_thick, b.mouth_thick, t);
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

static int16_t bodyBobFor(Expression s, uint32_t now) {
  const uint16_t period = MotionBehaviors::periodMsFor(s);
  if (period == 0) return 0;

  int8_t amp = 0;
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
      return 0;
  }

  const float t = (float)(now % period) / (float)period;
  return (int16_t)(-sinf(t * 2.0f * (float)PI) * (float)amp);
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
  sTo = targetForExpression(newExpr, kBaseTargets);
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
    sTo = targetForExpression(sNow, kBaseTargets);
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
  }

  const float tw = (float)(now - sTweenStartMs) / (float)kTweenMs;
  const float te = smoothstep01(tw);
  FaceParams p = lerpParams(sFrom, sTo, te);

  const uint32_t moodDt = (sLastMoodMs == 0) ? 0 : (now - sLastMoodMs);
  const float alpha = 1.0f - expf(-(float)moodDt / kMoodRingTauMs);
  const FaceParams moodTarget = targetForExpression(sNow, kBaseTargets);
  sMoodR += ((float)moodTarget.ring_r - sMoodR) * alpha;
  sMoodG += ((float)moodTarget.ring_g - sMoodG) * alpha;
  sMoodB += ((float)moodTarget.ring_b - sMoodB) * alpha;
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

  if (sNow != Expression::Joyful && sNow != Expression::VerbSleeping) {
    const int16_t b = (int16_t)(breathPhase(now) * 1.5f);
    p.eye_dy = (int16_t)(p.eye_dy + b);
    p.mouth_dy = (int16_t)(p.mouth_dy + b / 2);
  }

  p.face_y = (int16_t)(p.face_y + bodyBobFor(sNow, now));

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
