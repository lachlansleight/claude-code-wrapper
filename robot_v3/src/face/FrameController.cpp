#include "FrameController.h"

#include <esp_random.h>
#include <math.h>

#include "FACE_CONFIG.h"
#include "VerbTimeline.h"
#include "../behaviour/EmotionBlend.h"
#include "../hal/Display.h"
#include "../hal/MotionBehaviors.h"
#include "../hal/Settings.h"
#include "Scene.h"
#include "TextScene.h"

#include <string.h>

namespace Face {

static constexpr float kMoodRingTauMs = 200.0f;
static constexpr float kEmotionGeometrySmoothTauMs = 250.0f;

static constexpr uint32_t kTickIntervalMs = 33;
static constexpr uint32_t kTickIntervalStreamMs = 16;

static constexpr uint32_t kThinkingFlipDurMs = 600;
static constexpr uint32_t kThinkingFlipMinMs = 3000;
static constexpr uint32_t kThinkingFlipMaxMs = 6000;

static int16_t sLastExprIdx = -1;

static FaceParams sSmoothedEmotion;
static FaceParams sLastRendered;
static uint32_t sLastEmotionSmoothMs = 0;

static bool verbUsesTimeline(Expression s) {
  switch (s) {
    case Expression::VerbThinking:
    case Expression::VerbReading:
    case Expression::VerbWriting:
    case Expression::VerbExecuting:
    case Expression::VerbStraining:
    case Expression::VerbSleeping:
      return true;
    default:
      return false;
  }
}

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

// Body vertical bob: integrate phase so a changing waggle period (blended
// emotion) does not re-phase from wall-clock % period (which jitters).
static float sBodyBobPhaseRad = 0.0f;
static uint32_t sBodyBobPhaseLastMs = 0;

static int16_t lerpi(int16_t a, int16_t b, float t) { return (int16_t)(a + (b - a) * t); }

static FaceConfig::IdleAnimRow idleFor(const SceneContext& ctx, Expression s) {
  if (Face::isEmotionExpression(s)) {
    return EmotionBlend::blendedIdleAnim(ctx.mood_v, ctx.mood_a);
  }
  return FaceConfig::kIdleAnim[(uint8_t)s];
}

static float breathPhase(uint32_t now) {
  const float t = (float)(now % 4000) / 4000.0f;
  return sinf(t * 2.0f * (float)PI);
}

static int16_t bodyBobFor(const SceneContext& ctx, const FaceConfig::IdleAnimRow& idle,
                          uint32_t now) {
  const Expression s = ctx.effective_expression;
  const uint16_t period = MotionBehaviors::periodMsForContext(ctx);
  if (period == 0) {
    sBodyBobPhaseLastMs = now;
    return 0;
  }

  int16_t amp = 0;
  bool integrate = false;
  if (idle.bob_amplitude_px == FaceConfig::kBobAmpFollowEmotionArm) {
    integrate = true;
    if (Face::isEmotionExpression(s) &&
        ctx.base_emotion_arm.min_offset_deg != ctx.base_emotion_arm.max_offset_deg) {
      amp = 3;
    } else {
      amp = 0;
    }
  } else {
    amp = idle.bob_amplitude_px;
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

static void gazeFor(const FaceConfig::IdleAnimRow& idle, uint32_t now, int16_t& gdx,
                    int16_t& gdy) {
  gdx = 0;
  gdy = 0;
  switch (idle.gaze_style) {
    case FaceConfig::GazeStyle::IdleRandom: {
      const uint32_t moveMs = idle.gaze_move_ms ? idle.gaze_move_ms : 200u;
      if (sIdleGlanceStartMs != 0) {
        const float t = smoothstep01((float)(now - sIdleGlanceStartMs) / (float)moveMs);
        gdx = lerpi(sIdleGlanceFromDx, sIdleGlanceDx, t);
        gdy = lerpi(sIdleGlanceFromDy, sIdleGlanceDy, t);
      } else {
        gdx = sIdleGlanceDx;
        gdy = sIdleGlanceDy;
      }

      if (sNextIdleGlanceMs == 0 || now >= sNextIdleGlanceMs) {
        sIdleGlanceFromDx = gdx;
        sIdleGlanceFromDy = gdy;
        const int32_t sx = idle.gaze_rand_span_x > 0 ? (int32_t)idle.gaze_rand_span_x : 0;
        const int32_t sy = idle.gaze_rand_span_y > 0 ? (int32_t)idle.gaze_rand_span_y : 0;
        sIdleGlanceDx = (int16_t)random(-sx, sx + 1);
        sIdleGlanceDy = (int16_t)random(-sy, sy + 1);
        sIdleGlanceStartMs = now;
        if (idle.gaze_reroll_max_ms >= idle.gaze_reroll_min_ms) {
          sNextIdleGlanceMs =
              now + (uint32_t)random((long)idle.gaze_reroll_min_ms,
                                     (long)idle.gaze_reroll_max_ms + 1);
        } else {
          sNextIdleGlanceMs = now + 1000u;
        }
      }
      break;
    }
    case FaceConfig::GazeStyle::Orbit: {
      const uint32_t per = idle.gaze_scan_period_ms;
      if (per == 0) break;
      const float t = (float)(now % per) / (float)per;
      gdx = (int16_t)(sinf(t * 2.0f * (float)PI) * (float)idle.gaze_amp_x);
      gdy = (int16_t)(cosf(t * 2.0f * (float)PI) * (float)idle.gaze_amp_y);
      break;
    }
    case FaceConfig::GazeStyle::ScanX: {
      const uint32_t per = idle.gaze_scan_period_ms;
      if (per == 0) break;
      const float t = (float)(now % per) / (float)per;
      gdx = (int16_t)(sinf(t * 2.0f * (float)PI) * (float)idle.gaze_amp_x);
      break;
    }
    case FaceConfig::GazeStyle::Off:
    default:
      break;
  }
}

static void scheduleNextBlink(const FaceConfig::IdleAnimRow& idle, uint32_t from) {
  if (idle.blink_period_min_ms == 0 && idle.blink_period_max_ms == 0) {
    sNextBlinkMs = 0;
    return;
  }
  if (idle.blink_period_max_ms < idle.blink_period_min_ms) {
    sNextBlinkMs = 0;
    return;
  }
  const uint32_t p = (uint32_t)random((long)idle.blink_period_min_ms,
                                      (long)idle.blink_period_max_ms + 1);
  sNextBlinkMs = from + p;
}

static float currentBlinkAmount(uint32_t now, const FaceConfig::IdleAnimRow& idle) {
  if (!sBlinkActive) return 0.0f;
  const uint32_t closeMs = idle.blink_close_ms ? idle.blink_close_ms : 80u;
  const uint32_t openMs = idle.blink_open_ms ? idle.blink_open_ms : 130u;
  const uint32_t d = now - sBlinkStartMs;
  if (d < closeMs) {
    return (float)d / (float)closeMs;
  }
  const uint32_t d2 = d - closeMs;
  if (d2 < openMs) {
    return 1.0f - (float)d2 / (float)openMs;
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
  sSmoothedEmotion = baseTargetFor(Expression::Neutral);
  sLastRendered = sSmoothedEmotion;
  sLastEmotionSmoothMs = 0;
  sNextBlinkMs = 0;
  sBlinkActive = false;
  sLastTickMs = 0;

  sThinkFromSign = 1.0f;
  sThinkToSign = 1.0f;
  sThinkFlipStartMs = 0;
  sNextThinkFlipMs = 0;

  sMoodR = (float)sSmoothedEmotion.ring_r.value;
  sMoodG = (float)sSmoothedEmotion.ring_g.value;
  sMoodB = (float)sSmoothedEmotion.ring_b.value;
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
  if (idx >= (uint8_t)Expression::Count) return FaceConfig::kBaseTargets[0];
  return FaceConfig::kBaseTargets[idx];
}

static void onExpressionChange(Expression newExpr, uint32_t now, const SceneContext& ctx) {
  FaceParams currentFrame = sLastRendered;

  const bool hadOld = (sLastExprIdx >= 0);
  const Expression oldExpr = hadOld ? (Expression)(uint8_t)sLastExprIdx : Expression::VerbSleeping;

  if (hadOld && oldExpr == Expression::VerbThinking && newExpr != Expression::VerbThinking) {
    const float sign = currentThinkSign(now);
    currentFrame.face_rot.value = (int16_t)((float)currentFrame.face_rot.value * sign);
    currentFrame.pupil_dx.value = (int16_t)((float)currentFrame.pupil_dx.value * sign);
  }

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
  const FaceConfig::IdleAnimRow idleNew = idleFor(ctx, newExpr);
  scheduleNextBlink(idleNew, now);

  if (newExpr == Expression::VerbThinking) {
    resetThinkTilt(now);
  }

  if (idleNew.gaze_style == FaceConfig::GazeStyle::IdleRandom) {
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
    smoothFaceValuesToward(sSmoothedEmotion, ctx.base_face_params, 1.0f);
    sMoodR = (float)ctx.base_face_params.ring_r.value;
    sMoodG = (float)ctx.base_face_params.ring_g.value;
    sMoodB = (float)ctx.base_face_params.ring_b.value;
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

  const FaceConfig::IdleAnimRow idle = idleFor(ctx, sNow);

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

  const uint32_t emoDt = (sLastEmotionSmoothMs == 0) ? tickInterval : (now - sLastEmotionSmoothMs);
  sLastEmotionSmoothMs = now;
  const float emoAlpha = 1.0f - expf(-(float)emoDt / kEmotionGeometrySmoothTauMs);
  smoothFaceValuesToward(sSmoothedEmotion, ctx.base_face_params, emoAlpha);

  bool verbHas[(size_t)FieldIndex::Count];
  ParamI16 verbVals[(size_t)FieldIndex::Count];
  memset(verbHas, 0, sizeof(verbHas));
  if (verbUsesTimeline(sNow)) {
    sampleVerbTimeline(sNow, ctx.verb_time_in_current_ms, verbHas, verbVals);
  }

  FaceParams p = combineEmotionVerbFace(sSmoothedEmotion, verbHas, verbVals);

  const uint32_t moodDt = (sLastMoodMs == 0) ? 0 : (now - sLastMoodMs);
  const float moodAlpha = 1.0f - expf(-(float)moodDt / kMoodRingTauMs);
  sMoodR += ((float)p.ring_r.value - sMoodR) * moodAlpha;
  sMoodG += ((float)p.ring_g.value - sMoodG) * moodAlpha;
  sMoodB += ((float)p.ring_b.value - sMoodB) * moodAlpha;
  sLastMoodMs = now;

  if (sNow != Expression::Joyful && sNow != Expression::Gleeful &&
      sNow != Expression::VerbSleeping) {
    const int16_t b = (int16_t)(breathPhase(now) * 1.5f);
    p.eye_dy.value = (int16_t)(p.eye_dy.value + b);
    p.mouth_dy.value = (int16_t)(p.mouth_dy.value + b / 2);
  }

  p.face_y.value = (int16_t)(p.face_y.value + bodyBobFor(ctx, idle, now));

  if (sNow == Expression::VerbThinking) {
    maybeFlipThinkTilt(now);
    const float sign = currentThinkSign(now);
    p.face_rot.value = (int16_t)((float)p.face_rot.value * sign);
    p.pupil_dx.value = (int16_t)((float)p.pupil_dx.value * sign);
  }

  if (!sBlinkActive) {
    if (sNextBlinkMs == 0) {
      scheduleNextBlink(idle, now);
    } else if (now >= sNextBlinkMs) {
      sBlinkActive = true;
      sBlinkStartMs = now;
      sNextBlinkMs = 0;
    }
  }
  const float blinkAmt = currentBlinkAmount(now, idle);
  if (!sBlinkActive && sNextBlinkMs == 0) {
    scheduleNextBlink(idle, now);
  }

  int16_t gdx = 0, gdy = 0;
  gazeFor(idle, now, gdx, gdy);

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
  sLastRendered = p;
  Display::pushFrame();
}

}  // namespace Face
