#include "FrameController.h"

#include <esp_random.h>
#include <math.h>

#include "AgentEvents.h"
#include "Display.h"
#include "MotionBehaviors.h"
#include "Personality.h"
#include "Scene.h"
#include "SceneTypes.h"
#include "TextScene.h"

namespace Face {

static constexpr float   kMoodRingTauMs  = 200.0f;

// Per-state target params. Row order must match Personality::State.
//
// Curve convention (matches drawParabola):
//   bend > 0 → ∩ peak shape (happy eyes; frown mouth)
//   bend < 0 → ∪ sag shape  (sad eyes; smile mouth)
//
// face_rot represents the "default" tilt (sign = +1). The thinking
// modulator periodically flips this sign so the tilt swaps direction.
static const FaceParams kTargets[Personality::kStateCount] = {
  /* IDLE     */ {  2, 30, 26, 3,   0,  0,  3, 15,  0, 30,   -2,  0, 3,   0,  0,    0,   0,   0 },
  /* THINKING */ {  0, 30, 30, 3,   0,  7, -9, 15,  0, 22,  -3,  0, 3, -10,  0,   36,  56, 120 },  // dark blue
  /* READING  */ {  0, 28, 26, 3,   0,  0,  8, 12,  0, 18,  -3,  0, 3,   0, 12,   78, 146, 210 },  // looks down, round eyes, pupils low, slight smile
  /* WRITING  */ {  0, 30, 26, 3,   0,  0, -8, 15,  0, 30,  -1, 14, 3,   0,  0,  104, 118, 228 },  // light blue (more purple than thinking)
  /* EXECUTING */ {  0, 30, 16, 3,   0,  0, -4, 10,  0, 18,  -2,  0, 3,   0,  0,  156,  64, 216 },  // purple, narrow eyes, small smile
  /* EXEC_LONG */ {  0, 30, 22, 3,   0,  0, -3, 10,  0, 18,   0,  0, 3,   0,  0,  210,  75, 220 },  // slightly redder purple, wider eyes
  /* FINISHED */ { -4, 24,  4, 4,   7,  0,  0,  0,  0, 36,  -1, 14, 4,   0,  0,  255, 228,  32 },  // bright yellow
  /* EXCITED  */ {  0, 30, 30, 3,   0,  0,  0, 15,  0, 30,  -8,  0, 3,   0,  0,   40, 255,  80 },  // bright green
  /* READY    */ {  0, 30, 30, 3,   0,  0,  0, 15,  0, 26,  -3,  0, 3,   0,  0,    0,   0,   0 },
  /* WAKING   */ { -2, 34, 34, 3,   0,  0,  0, 18,  0, 14,   0,  9, 3,   0,  0,    0,   0,   0 },
  /* SLEEP    */ {  8, 26,  2, 3,   0,  0,  0,  0,  0, 18,   0,  0, 3,   0,  0,    0,   0,   0 },
  /* BLOCKED  */ {  2, 30, 22, 3,  -6,  0,  3, 15,  4, 26,   8,  0, 3,   0,  0,  255,  48,  24 },  // red
  /* WANTS_AT */ { -2, 34, 34, 3,   0,  0,  0, 18,  0, 14,   0,  9, 3,   0,  0,  255, 200,  40 },  // wide eyes + "oh!" mouth + amber ring
};

// Tween duration between state targets.
static constexpr uint32_t kTweenMs        = 250;
// Default render rate cap (~30 fps). When read/write code streams are active
// (or still fading), use kTickIntervalStreamMs so scroll tracks millis() more
// closely — otherwise a slow loop() makes the stream look ~5–8 Hz.
static constexpr uint32_t kTickIntervalMs       = 33;
static constexpr uint32_t kTickIntervalStreamMs = 16;  // ~60 fps cap while streams run

static constexpr uint32_t kBlinkCloseMs = 80;
static constexpr uint32_t kBlinkOpenMs  = 130;

// Thinking head-tilt flip — every kThinkingFlipMin/Max seconds the face
// rotation and pupil_dx flip sign, smoothly interpolated over kFlipDurMs.
static constexpr uint32_t kThinkingFlipDurMs = 600;
static constexpr uint32_t kThinkingFlipMinMs = 3000;
static constexpr uint32_t kThinkingFlipMaxMs = 6000;

// ---- Running state --------------------------------------------------------

static FaceParams sFrom;
static FaceParams sTo;
static uint32_t   sTweenStartMs = 0;
static Personality::State sLastState = Personality::kStateCount;

static uint32_t sNextBlinkMs  = 0;
static uint32_t sBlinkStartMs = 0;
static bool     sBlinkActive  = false;

static uint32_t sLastTickMs = 0;
static float    sMoodR = 0.0f;
static float    sMoodG = 0.0f;
static float    sMoodB = 0.0f;
static uint32_t sLastMoodMs = 0;
static uint32_t sProgressFadeStartMs = 0;
static uint16_t sFadeReadCount = 0;
static uint16_t sFadeWriteCount = 0;
static float    sTextStreamAlpha = 0.0f;
static float    sWriteStreamAlpha = 0.0f;
static uint32_t sLastEffectsMs = 0;

// Thinking tilt-flip state.
static float    sThinkFromSign     = 1.0f;
static float    sThinkToSign       = 1.0f;
static uint32_t sThinkFlipStartMs  = 0;
static uint32_t sNextThinkFlipMs   = 0;

// Idle glance state.
static int16_t  sIdleGlanceDx      = 0;
static int16_t  sIdleGlanceDy      = 0;
static int16_t  sIdleGlanceFromDx  = 0;
static int16_t  sIdleGlanceFromDy  = 0;
static uint32_t sIdleGlanceStartMs = 0;
static uint32_t sNextIdleGlanceMs  = 0;
static constexpr uint32_t kIdleGlanceTweenMs = 200;

// ---- Math helpers --------------------------------------------------------

static int16_t lerpi(int16_t a, int16_t b, float t) {
  return (int16_t)(a + (b - a) * t);
}

static FaceParams lerpParams(const FaceParams& a, const FaceParams& b, float t) {
  FaceParams r;
  r.eye_dy       = lerpi(a.eye_dy,       b.eye_dy,       t);
  r.eye_rx       = lerpi(a.eye_rx,       b.eye_rx,       t);
  r.eye_ry       = lerpi(a.eye_ry,       b.eye_ry,       t);
  r.eye_stroke   = lerpi(a.eye_stroke,   b.eye_stroke,   t);
  r.eye_curve    = lerpi(a.eye_curve,    b.eye_curve,    t);
  r.pupil_dx     = lerpi(a.pupil_dx,     b.pupil_dx,     t);
  r.pupil_dy     = lerpi(a.pupil_dy,     b.pupil_dy,     t);
  r.pupil_r      = lerpi(a.pupil_r,      b.pupil_r,      t);
  r.mouth_dy     = lerpi(a.mouth_dy,     b.mouth_dy,     t);
  r.mouth_w      = lerpi(a.mouth_w,      b.mouth_w,      t);
  r.mouth_curve  = lerpi(a.mouth_curve,  b.mouth_curve,  t);
  r.mouth_open_h = lerpi(a.mouth_open_h, b.mouth_open_h, t);
  r.mouth_thick  = lerpi(a.mouth_thick,  b.mouth_thick,  t);
  r.face_rot     = lerpi(a.face_rot,     b.face_rot,     t);
  r.face_y       = lerpi(a.face_y,       b.face_y,       t);
  r.ring_r       = lerpi(a.ring_r,       b.ring_r,       t);
  r.ring_g       = lerpi(a.ring_g,       b.ring_g,       t);
  r.ring_b       = lerpi(a.ring_b,       b.ring_b,       t);
  return r;
}

// ---- Modulators ---------------------------------------------------------

static float breathPhase(uint32_t now) {
  const float t = (float)(now % 4000) / 4000.0f;
  return sinf(t * 2.0f * (float)PI);
}

// Body-bob: a small vertical offset on `face_y` synced to the arm motor's
// period for `s`, so the face animation reads as part of the same body
// rhythm. Returns 0 for states without an associated motor period.
//
// To tune: amplitude per state lives below; the period is pulled from
// MotionBehaviors so changing a row in that table also re-syncs the face.
static int16_t bodyBobFor(Personality::State s, uint32_t now) {
  const uint16_t period = MotionBehaviors::periodMsFor(s);
  if (period == 0) return 0;

  int8_t amp = 0;
  switch (s) {
    // SLEEP — slow breath. Motor: OSCILLATE, period 8000 ms. The only
    //   face animation while sleeping, so make it visible.
    case Personality::SLEEP:           amp = 10; break;

    // EXECUTING family — body-bob in time with the arm sway.
    case Personality::EXECUTING:
    case Personality::EXECUTING_LONG:
    case Personality::EXCITED:         amp = 5;  break;

    // FINISHED — one head-bob per waggle (period is the full retrigger
    //   cycle), reads as "hey! hey!" excited bouncing.
    case Personality::FINISHED:        amp = 7;  break;

    default: return 0;
  }

  // Negative so that when the motor swings to its "up" end (positive
  // offset) the face also moves up. face_y > 0 shifts the face DOWN
  // (see FaceRenderer.cpp), so we invert the sine.
  const float t = (float)(now % period) / (float)period;
  return (int16_t)(-sinf(t * 2.0f * (float)PI) * (float)amp);
}

static void gazeFor(Personality::State s, uint32_t now,
                    int16_t& gdx, int16_t& gdy) {
  gdx = 0; gdy = 0;
  switch (s) {
    case Personality::IDLE: {
      if (sIdleGlanceStartMs != 0) {
        const float t = smoothstep01(
            (float)(now - sIdleGlanceStartMs) / (float)kIdleGlanceTweenMs);
        gdx = lerpi(sIdleGlanceFromDx, sIdleGlanceDx, t);
        gdy = lerpi(sIdleGlanceFromDy, sIdleGlanceDy, t);
      } else {
        gdx = sIdleGlanceDx;
        gdy = sIdleGlanceDy;
      }

      if (sNextIdleGlanceMs == 0 || now >= sNextIdleGlanceMs) {
        sIdleGlanceFromDx = gdx;
        sIdleGlanceFromDy = gdy;
        sIdleGlanceDx = (int16_t)random(-15, 16);  // safe horizontal range
        sIdleGlanceDy = (int16_t)random(-10, 11);  // safe vertical range
        sIdleGlanceStartMs = now;
        sNextIdleGlanceMs = now + (uint32_t)random(1000, 10001);
      }
      break;
    }
    case Personality::THINKING: {
      const float t = (float)(now % 900) / 900.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 2);
      gdy = (int16_t)(cosf(t * 2 * (float)PI) * 2);
      break;
    }
    case Personality::READING: {
      const float t = (float)(now % 1300) / 1300.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 6);
      break;
    }
    case Personality::WRITING: {
      const float t = (float)(now % 2200) / 2200.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 2);
      break;
    }
    case Personality::EXECUTING:
    case Personality::EXECUTING_LONG: {
      const float t = (float)(now % 2500) / 2500.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 1);
      break;
    }
    case Personality::EXCITED: {
      const float t = (float)(now % 3500) / 3500.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 3);
      gdy = (int16_t)(cosf(t * 2 * (float)PI) * 2);
      break;
    }
    case Personality::READY: {
      const float t = (float)(now % 5500) / 5500.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 2);
      break;
    }
    default: break;
  }
}

// ---- Blink scheduler ----------------------------------------------------

static uint32_t blinkPeriodMsFor(Personality::State s) {
  switch (s) {
    case Personality::IDLE:     return (uint32_t)random(4000, 6500);
    case Personality::THINKING: return (uint32_t)random(2000, 3500);
    case Personality::READING:  return (uint32_t)random(4000, 6000);
    case Personality::WRITING:  return (uint32_t)random(3500, 5500);
    case Personality::EXECUTING:
    case Personality::EXECUTING_LONG:
      return (uint32_t)random(4500, 7000);
    case Personality::EXCITED:  return (uint32_t)random(2500, 4000);
    case Personality::READY:    return (uint32_t)random(3000, 4500);
    default:                    return 0;
  }
}

static void scheduleNextBlink(Personality::State s, uint32_t from) {
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

// ---- Thinking tilt-flip --------------------------------------------------

// Returns the current tilt sign (smoothly interpolated during a flip).
// Sign multiplies face_rot and pupil_dx — sign = -1 mirrors the tilt.
static float currentThinkSign(uint32_t now) {
  if (sThinkFlipStartMs == 0) return sThinkToSign;
  const float t = (float)(now - sThinkFlipStartMs) / (float)kThinkingFlipDurMs;
  return sThinkFromSign +
         (sThinkToSign - sThinkFromSign) * smoothstep01(t);
}

static void resetThinkTilt(uint32_t now) {
  sThinkFromSign    = 1.0f;
  sThinkToSign      = 1.0f;
  sThinkFlipStartMs = 0;
  sNextThinkFlipMs  = now + (uint32_t)random((long)kThinkingFlipMinMs,
                                             (long)kThinkingFlipMaxMs + 1);
}

static void maybeFlipThinkTilt(uint32_t now) {
  if (sNextThinkFlipMs == 0 || now < sNextThinkFlipMs) return;
  sThinkFromSign    = currentThinkSign(now);
  sThinkToSign      = -sThinkFromSign;
  sThinkFlipStartMs = now;
  sNextThinkFlipMs  = now + kThinkingFlipDurMs +
                      (uint32_t)random((long)kThinkingFlipMinMs,
                                       (long)kThinkingFlipMaxMs + 1);
}

static constexpr uint32_t kProgressFadeMs   = 280;
static constexpr uint32_t kEffectsFadeMs    = 100;
// ---- Lifecycle ---------------------------------------------------------

void begin() {
  randomSeed(esp_random());

  sLastState    = Personality::kStateCount;
  // Seed the initial frame to match the boot personality state.
  sFrom         = kTargets[Personality::SLEEP];
  sTo           = sFrom;
  sTweenStartMs = millis();
  sNextBlinkMs  = 0;
  sBlinkActive  = false;
  sLastTickMs   = 0;

  // Thinking tilt sane defaults — only matters once we enter THINKING.
  sThinkFromSign    = 1.0f;
  sThinkToSign      = 1.0f;
  sThinkFlipStartMs = 0;
  sNextThinkFlipMs  = 0;

  sMoodR = (float)kTargets[Personality::SLEEP].ring_r;
  sMoodG = (float)kTargets[Personality::SLEEP].ring_g;
  sMoodB = (float)kTargets[Personality::SLEEP].ring_b;
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
}

void invalidate() {
  sLastTickMs = 0;
}

static void onStateChange(Personality::State newState, uint32_t now) {
  // Capture the current interpolated frame as the new "from".
  const float t = (float)(now - sTweenStartMs) / (float)kTweenMs;
  FaceParams currentFrame = lerpParams(sFrom, sTo, smoothstep01(t));

  // If we're leaving THINKING, bake the active tilt sign into the
  // captured frame so the un-rotation tweens correctly back to upright.
  if (sLastState == Personality::THINKING && newState != Personality::THINKING) {
    const float sign = currentThinkSign(now);
    currentFrame.face_rot = (int16_t)((float)currentFrame.face_rot * sign);
    currentFrame.pupil_dx = (int16_t)((float)currentFrame.pupil_dx * sign);
  }

  sFrom         = currentFrame;
  sTo           = kTargets[newState];
  sTweenStartMs = now;
  if (sLastState == Personality::READY && newState == Personality::IDLE) {
    const AgentEvents::AgentState& cs = AgentEvents::state();
    sFadeReadCount = cs.read_tools_this_turn;
    sFadeWriteCount = cs.write_tools_this_turn;
    sProgressFadeStartMs = now;
  } else {
    sProgressFadeStartMs = 0;
    sFadeReadCount = 0;
    sFadeWriteCount = 0;
  }
  sLastState    = newState;

  sBlinkActive = false;
  scheduleNextBlink(newState, now);

  if (newState == Personality::THINKING) {
    resetThinkTilt(now);
  }

  if (newState == Personality::IDLE) {
    // Trigger an immediate fresh glance when entering idle.
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

void tick() {
  if (!Display::ready()) return;

  const uint32_t now = millis();
  const Personality::State sNow = Personality::current();
  const bool streamFrame =
      sNow == Personality::READING || sNow == Personality::WRITING ||
      sTextStreamAlpha > 0.02f || sWriteStreamAlpha > 0.02f;
  const uint32_t tickInterval =
      streamFrame ? kTickIntervalStreamMs : kTickIntervalMs;
  if (now - sLastTickMs < tickInterval) return;
  sLastTickMs = now;

  const Personality::State s = sNow;
  if (s != sLastState) {
    onStateChange(s, now);
  }

  // Tween progress
  const float t  = (float)(now - sTweenStartMs) / (float)kTweenMs;
  const float te = smoothstep01(t);
  FaceParams p   = lerpParams(sFrom, sTo, te);

  // Shared mood color easing with a 200ms time constant.
  const uint32_t moodDt = (sLastMoodMs == 0) ? 0 : (now - sLastMoodMs);
  const float alpha = 1.0f - expf(-(float)moodDt / kMoodRingTauMs);
  const FaceParams& moodTarget = kTargets[s];
  sMoodR += ((float)moodTarget.ring_r - sMoodR) * alpha;
  sMoodG += ((float)moodTarget.ring_g - sMoodG) * alpha;
  sMoodB += ((float)moodTarget.ring_b - sMoodB) * alpha;
  sLastMoodMs = now;

  // Effects fade: read stream in READING; write stream (lower band) in WRITING.
  const uint32_t effectsDt = (sLastEffectsMs == 0) ? 0 : (now - sLastEffectsMs);
  const float effectsA = 1.0f - expf(-(float)effectsDt / (float)kEffectsFadeMs);
  const float readTarget = (s == Personality::READING) ? 1.0f : 0.0f;
  const float writeTarget = (s == Personality::WRITING) ? 1.0f : 0.0f;
  sTextStreamAlpha += (readTarget - sTextStreamAlpha) * effectsA;
  sWriteStreamAlpha += (writeTarget - sWriteStreamAlpha) * effectsA;
  sLastEffectsMs = now;

  if (sProgressFadeStartMs != 0 && now - sProgressFadeStartMs >= kProgressFadeMs) {
    sProgressFadeStartMs = 0;
    sFadeReadCount = 0;
    sFadeWriteCount = 0;
  }

  // Breath — universal idle modulator on eye/mouth y, fixed 4 s sine.
  // Suppressed in FINISHED (face is celebrating) and SLEEP (replaced by
  // the slower motor-synced body-bob below).
  if (s != Personality::FINISHED && s != Personality::SLEEP) {
    const int16_t b = (int16_t)(breathPhase(now) * 1.5f);
    p.eye_dy   = (int16_t)(p.eye_dy   + b);
    p.mouth_dy = (int16_t)(p.mouth_dy + b / 2);
  }

  // Body-bob — adds a vertical offset on face_y synced to the motor
  // period for states that have rhythmic arm motion. No-op for states
  // without a configured period.
  p.face_y = (int16_t)(p.face_y + bodyBobFor(s, now));

  // Thinking tilt-flip — modulates face_rot and pupil_dx by ±1 sign.
  // Applies to the lerped p (so during an in-tween e.g. IDLE→THINKING
  // the rotation builds up gradually as expected).
  if (s == Personality::THINKING) {
    maybeFlipThinkTilt(now);
    const float sign = currentThinkSign(now);
    p.face_rot = (int16_t)((float)p.face_rot * sign);
    p.pupil_dx = (int16_t)((float)p.pupil_dx * sign);
  }

  // Blink scheduler
  if (!sBlinkActive) {
    if (sNextBlinkMs == 0) {
      scheduleNextBlink(s, now);
    } else if (now >= sNextBlinkMs) {
      sBlinkActive  = true;
      sBlinkStartMs = now;
      sNextBlinkMs  = 0;
    }
  }
  const float blinkAmt = currentBlinkAmount(now);
  if (!sBlinkActive && sNextBlinkMs == 0) {
    scheduleNextBlink(s, now);
  }

  // Gaze wander
  int16_t gdx = 0, gdy = 0;
  gazeFor(s, now, gdx, gdy);

  // Render + push
  TFT_eSprite& spr = Display::sprite();
  SceneRenderState renderState = {
      s,       sMoodR, sMoodG, sMoodB, sTextStreamAlpha, sWriteStreamAlpha,
      sProgressFadeStartMs, sFadeReadCount, sFadeWriteCount};
  if (AgentEvents::renderMode() == AgentEvents::RENDER_TEXT) {
    renderTextScene(spr, renderState, AgentEvents::state(), now);
  } else {
    renderScene(spr, p, blinkAmt, gdx, gdy, renderState, AgentEvents::state(), now);
  }
  Display::pushFrame();
}

}  // namespace Face
