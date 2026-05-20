#include "FrameController.h"

#include <esp_random.h>
#include <math.h>

#include "FACE_CONFIG.h"
#include "FrameEffective.h"
#include "VerbTimeline.h"
#include "../behaviour/EmotionBlend.h"
#include "../hal/Display.h"
#include "../hal/Motion.h"
#include "../hal/Settings.h"
#include "Scene.h"
#include "TextScene.h"

#include <string.h>

namespace Face {

static int16_t sLastExprIdx = -1;

static FaceParams sLastRendered;

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

static uint32_t sNextBlinkMs = 0;
static int16_t sIdleGlanceDx = 0;
static int16_t sIdleGlanceDy = 0;
static int16_t sIdleGlanceFromDx = 0;
static int16_t sIdleGlanceFromDy = 0;
static uint32_t sIdleGlanceStartMs = 0;
static uint32_t sNextIdleGlanceMs = 0;

// Last-rendered values + snapshots for cross-fading the modification pass
// across verb transitions. Captured into sFrom* on verb change and lerped
// toward live values via `Face::verbTransitionT(now)`.
static int16_t sLastBobAmp = 0;
static int16_t sLastGdx = 0;
static int16_t sLastGdy = 0;
static int16_t sFromBobAmp = 0;
static int16_t sFromGdx = 0;
static int16_t sFromGdy = 0;
static Expression sLastVerbForXfade = Expression::Count;

// Integrated phases for periodic outputs whose period/speed can change
// continuously per frame (EmotionBlend smoothly interpolates idle anim and
// FaceParams as V/A drifts). Computing phase as `now % period` or
// `speed * now` would re-derive an absolute angle from a moving denominator
// each frame, producing high-frequency jitter — instead we accumulate
// `phase += dPhase * dt` so phase stays continuous across the drift.
static float sGazePhaseRad = 0.0f;
static uint32_t sGazePhaseLastMs = 0;
static float sEyeWavePhaseRad = 0.0f;
static float sMouthWavePhaseRad = 0.0f;
static uint32_t sWavePhaseLastMs = 0;

static int16_t lerpi(int16_t a, int16_t b, float t) { return (int16_t)(a + (b - a) * t); }

static FaceConfig::IdleAnimRow idleFor(const SceneContext& ctx, Expression s) {
  if (Face::isEmotionExpression(s)) {
    return EmotionBlend::blendedIdleAnim(ctx.mood_v, ctx.mood_a);
  }
  return FaceConfig::kIdleAnim[(uint8_t)s];
}

static const FaceConfig::FrameAnimConfig& animCfg() { return FaceConfig::kFrameAnim; }

static float breathPhase(uint32_t now) {
  const uint32_t periodMs = animCfg().breath_period_ms ? animCfg().breath_period_ms : 4000u;
  const float t = (float)(now % periodMs) / (float)periodMs;
  return sinf(t * 2.0f * (float)PI);
}

static int16_t bodyBobAmpFor(const FaceConfig::IdleAnimRow& idle,
                             const FaceParams& effective) {
  if (idle.bob_amplitude_px == FaceConfig::kBobAmpFollowEmotionArm) {
    if (effective.arm_min_deg.value != effective.arm_max_deg.value) {
      return animCfg().emotion_bob_amp_follow_arm;
    }
    return 0;
  }
  return idle.bob_amplitude_px;
}

/** Map live servo offset (deg) to vertical face bob (px), scaled by idle row. */
static int16_t bodyBobFor(const FaceConfig::IdleAnimRow& idle, const FaceParams& effective) {
  const int16_t amp = bodyBobAmpFor(idle, effective);
  sLastBobAmp = amp;
  if (amp == 0) return 0;

  int16_t lo = effective.arm_min_deg.value;
  int16_t hi = effective.arm_max_deg.value;
  if (lo > hi) {
    const int16_t t = lo;
    lo = hi;
    hi = t;
  }
  if (lo == hi) return 0;

  const int8_t armDeg = Motion::currentOffsetDeg();
  float t = (float)(armDeg - lo) / (float)(hi - lo);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return (int16_t)lroundf((float)amp * (2.0f * t - 1.0f));
}

static void gazeFor(const FaceConfig::IdleAnimRow& idle, uint32_t now, int16_t& gdx,
                    int16_t& gdy) {
  gdx = 0;
  gdy = 0;

  // Advance the gaze phase regardless of style so switching from
  // Off/IdleRandom into Orbit/ScanX has a sensible starting phase. Period
  // is read live (interpolated by EmotionBlend); integrating dt keeps the
  // result continuous across V/A drift.
  const uint32_t per = idle.gaze_scan_period_ms;
  if (per != 0) {
    const float dtMs =
        (sGazePhaseLastMs == 0) ? 0.0f : (float)(now - sGazePhaseLastMs);
    sGazePhaseRad += (2.0f * (float)PI / (float)per) * dtMs;
    sGazePhaseRad = fmodf(sGazePhaseRad, 2.0f * (float)PI);
    if (sGazePhaseRad < 0.0f) sGazePhaseRad += 2.0f * (float)PI;
  }
  sGazePhaseLastMs = now;

  switch (idle.gaze_style) {
    case FaceConfig::GazeStyle::IdleRandom: {
      const uint32_t moveMs =
          idle.gaze_move_ms ? idle.gaze_move_ms : (uint32_t)animCfg().default_gaze_move_ms;
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
          sNextIdleGlanceMs = now + animCfg().invalid_gaze_reroll_fallback_ms;
        }
      }
      break;
    }
    case FaceConfig::GazeStyle::Orbit: {
      if (per == 0) break;
      gdx = (int16_t)(sinf(sGazePhaseRad) * (float)idle.gaze_amp_x);
      gdy = (int16_t)(cosf(sGazePhaseRad) * (float)idle.gaze_amp_y);
      break;
    }
    case FaceConfig::GazeStyle::ScanX: {
      if (per == 0) break;
      gdx = (int16_t)(sinf(sGazePhaseRad) * (float)idle.gaze_amp_x);
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
  const uint32_t closeMs = idle.blink_close_ms ? idle.blink_close_ms : animCfg().default_blink_close_ms;
  const uint32_t openMs = idle.blink_open_ms ? idle.blink_open_ms : animCfg().default_blink_open_ms;
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

void begin() {
  randomSeed(esp_random());

  effectiveParamsBegin();
  resetVerbTransition();
  sLastBobAmp = 0;
  sLastGdx = 0;
  sLastGdy = 0;
  sFromBobAmp = 0;
  sFromGdx = 0;
  sFromGdy = 0;
  sLastVerbForXfade = Expression::Count;
  sGazePhaseRad = 0.0f;
  sGazePhaseLastMs = 0;
  sEyeWavePhaseRad = 0.0f;
  sMouthWavePhaseRad = 0.0f;
  sWavePhaseLastMs = 0;
  sLastExprIdx = -1;
  sLastRendered = effectiveFaceParams();
  sNextBlinkMs = 0;
  sBlinkActive = false;
  sLastTickMs = 0;

  sMoodR = (float)sLastRendered.ring_r.value;
  sMoodG = (float)sLastRendered.ring_g.value;
  sMoodB = (float)sLastRendered.ring_b.value;
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

void invalidate() {
  sLastTickMs = 0;
  invalidateEffectiveParams();
}

const FaceParams& baseTargetFor(Expression e) {
  const uint8_t idx = (uint8_t)e;
  if (idx >= (uint8_t)Expression::Count) return FaceConfig::kBaseTargets[0];
  return FaceConfig::kBaseTargets[idx];
}

static void onExpressionChange(Expression newExpr, uint32_t now, const SceneContext& ctx) {
  const bool hadOld = (sLastExprIdx >= 0);
  const Expression oldExpr = hadOld ? (Expression)(uint8_t)sLastExprIdx : Expression::VerbSleeping;

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
    invalidateEffectiveParams();
    sMoodR = (float)ctx.base_face_params.ring_r.value;
    sMoodG = (float)ctx.base_face_params.ring_g.value;
    sMoodB = (float)ctx.base_face_params.ring_b.value;
    sLastMoodMs = now;
  }

  const bool streamFrame = (sNow == Expression::VerbReading || sNow == Expression::VerbWriting ||
                            sTextStreamAlpha > 0.02f || sWriteStreamAlpha > 0.02f);
  const uint32_t tickInterval =
      streamFrame ? animCfg().tick_interval_stream_ms : animCfg().tick_interval_ms;
  if (now - sLastTickMs < tickInterval) return;
  sLastTickMs = now;

  const int16_t idx = (int16_t)exprIdx;
  if (idx != sLastExprIdx) {
    onExpressionChange(sNow, now, ctx);
  }

  const FaceConfig::IdleAnimRow idle = idleFor(ctx, sNow);

  const uint32_t effectsDt = (sLastEffectsMs == 0) ? 0 : (now - sLastEffectsMs);
  const float effectsA = 1.0f - expf(-(float)effectsDt / (float)animCfg().effects_fade_ms);
  const float readTarget = (sNow == Expression::VerbReading) ? 1.0f : 0.0f;
  const float writeTarget = (sNow == Expression::VerbWriting) ? 1.0f : 0.0f;
  sTextStreamAlpha += (readTarget - sTextStreamAlpha) * effectsA;
  sWriteStreamAlpha += (writeTarget - sWriteStreamAlpha) * effectsA;
  sLastEffectsMs = now;

  if (sProgressFadeStartMs != 0 && now - sProgressFadeStartMs >= animCfg().progress_fade_ms) {
    sProgressFadeStartMs = 0;
    sFadeReadCount = 0;
    sFadeWriteCount = 0;
  }

  if (sNow != sLastVerbForXfade) {
    sFromBobAmp = sLastBobAmp;
    sFromGdx = sLastGdx;
    sFromGdy = sLastGdy;
    sLastVerbForXfade = sNow;
  }

  FaceParams p = effectiveFaceParams();

  const uint32_t moodDt = (sLastMoodMs == 0) ? 0 : (now - sLastMoodMs);
  const float moodAlpha = 1.0f - expf(-(float)moodDt / animCfg().mood_ring_tau_ms);
  sMoodR += ((float)p.ring_r.value - sMoodR) * moodAlpha;
  sMoodG += ((float)p.ring_g.value - sMoodG) * moodAlpha;
  sMoodB += ((float)p.ring_b.value - sMoodB) * moodAlpha;
  sLastMoodMs = now;

  if (!expressionUsesVerbTimeline(sNow) && sNow != Expression::Joyful && sNow != Expression::Gleeful &&
      sNow != Expression::VerbSleeping) {
    const int16_t b = (int16_t)(breathPhase(now) * animCfg().breath_eye_amp_px);
    p.eye_dy.value = (int16_t)(p.eye_dy.value + b);
    p.mouth_dy.value = (int16_t)(p.mouth_dy.value + (int16_t)((float)b * animCfg().breath_mouth_scale));
  }

  p.face_y.value = (int16_t)(p.face_y.value + bodyBobFor(idle, p));

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

  int16_t liveGdx = 0, liveGdy = 0;
  if (!expressionUsesVerbTimeline(sNow)) {
    gazeFor(idle, now, liveGdx, liveGdy);
  }
  // Cross-fade gaze across the verb transition window so picking up a new
  // verb's gaze pattern (Orbit / ScanX / IdleRandom) doesn't snap.
  const float ttGaze = verbTransitionT(now);
  int16_t gdx, gdy;
  if (ttGaze >= 1.0f) {
    gdx = liveGdx;
    gdy = liveGdy;
  } else {
    gdx = (int16_t)lroundf((float)sFromGdx + ((float)liveGdx - (float)sFromGdx) * ttGaze);
    gdy = (int16_t)lroundf((float)sFromGdy + ((float)liveGdy - (float)sFromGdy) * ttGaze);
  }
  sLastGdx = gdx;
  sLastGdy = gdy;

  const uint16_t fg565 = rgb888To565(ctx.fg_r, ctx.fg_g, ctx.fg_b);
  const uint16_t bg565 = rgb888To565(ctx.bg_r, ctx.bg_g, ctx.bg_b);
  const uint16_t divider565 = Settings::color565Scaled(Settings::NamedColor::Foreground, 96);

  TFT_eSprite& spr = Display::sprite();
  // Integrate wave phases from the resolved (smoothed) speeds. Doing this
  // here — *after* the emotion / verb blend has already produced final
  // FaceParams for the frame — means that even when speed is being
  // continuously interpolated by EmotionBlend, the phase stays continuous.
  // Computing `phase = speed * nowMs * π/180000` inside the renderer would
  // multiply a moving speed by a large nowMs and produce huge per-frame
  // phase jumps.
  {
    const float waveDtMs =
        (sWavePhaseLastMs == 0) ? 0.0f : (float)(now - sWavePhaseLastMs);
    sWavePhaseLastMs = now;
    constexpr float kPiOver180000 = (float)(M_PI / 180000.0);
    sEyeWavePhaseRad += (float)p.eye_wave_speed.value * waveDtMs * kPiOver180000;
    sMouthWavePhaseRad += (float)p.mouth_wave_speed.value * waveDtMs * kPiOver180000;
    constexpr float kTwoPi = 2.0f * (float)M_PI;
    sEyeWavePhaseRad = fmodf(sEyeWavePhaseRad, kTwoPi);
    if (sEyeWavePhaseRad < 0.0f) sEyeWavePhaseRad += kTwoPi;
    sMouthWavePhaseRad = fmodf(sMouthWavePhaseRad, kTwoPi);
    if (sMouthWavePhaseRad < 0.0f) sMouthWavePhaseRad += kTwoPi;
  }

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
                                  divider565,
                                  sEyeWavePhaseRad,
                                  sMouthWavePhaseRad};

  if (ctx.render_mode == (uint8_t)RenderMode::Face) {
    renderScene(spr, p, blinkAmt, gdx, gdy, renderState, ctx, now);
  } else {
    renderTextScene(spr, renderState, ctx, now);
  }
  sLastRendered = p;
  Display::pushFrame();
}

}  // namespace Face
