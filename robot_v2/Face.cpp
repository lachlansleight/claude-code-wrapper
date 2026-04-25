#include "Face.h"

#include <TFT_eSPI.h>
#include <esp_random.h>
#include <math.h>

#include "Display.h"
#include "Personality.h"

namespace Face {

// ---- Parameters -----------------------------------------------------------
//
// FaceParams is the per-frame vocabulary. Each state has a target
// FaceParams; transitions tween between them; per-frame modulators (blink,
// breath, gaze wander) layer on top.

struct FaceParams {
  int16_t eye_dy;        // vertical offset of both eyes from baseline
  int16_t eye_rx;        // eye ellipse half-width
  int16_t eye_ry;        // eye ellipse half-height
  int16_t eye_stroke;    // ring outline thickness
  int16_t eye_curve;     // !=0 → render as parabolic stroke; +=∩ (happy), −=∪ (sad)
  int16_t pupil_dx;
  int16_t pupil_dy;
  int16_t pupil_r;       // 0 = no pupil (e.g. happy closed eyes)
  int16_t mouth_dy;
  int16_t mouth_w;
  int16_t mouth_curve;   // parabola bend: +=frown (∩), −=smile (∪), 0=flat
  int16_t mouth_open_h;  // if >0, mouth renders as filled oval of this half-height
  int16_t mouth_thick;
};

// Baseline geometry
static constexpr int16_t kCx     = 120;
static constexpr int16_t kEyeY   = 95;
static constexpr int16_t kEyeLX  = 85;
static constexpr int16_t kEyeRX  = 155;
static constexpr int16_t kMouthY = 165;

static constexpr uint16_t kFg = TFT_WHITE;
static constexpr uint16_t kBg = TFT_BLACK;

// Per-state target params. Row order must match Personality::State.
//
// Curve convention (matches drawParabola):
//   bend > 0 → ∩ peak shape (happy eyes; frown mouth)
//   bend < 0 → ∪ sag shape  (sad eyes; smile mouth)
static const FaceParams kTargets[Personality::kStateCount] = {
  /* IDLE     */ {  2, 30, 22, 3,   0,  0,  3, 10,  0, 30,  1, 0, 3 },  // mouth: tiny bored frown
  /* THINKING */ {  0, 30, 30, 3,   0,  7, -9, 10,  0, 20,  0, 0, 3 },
  /* READING  */ {  0, 32, 16, 3,   0,  0,  0,  8,  0, 18,  0, 0, 3 },
  /* WRITING  */ {  0, 30, 26, 3,   0,  0,  8, 10,  0, 16,  0, 5, 3 },
  /* FINISHED */ { -4, 34,  4, 4,  12,  0,  0,  0,  0, 46,-16, 0, 4 },  // ∩ happy eyes, ∪ big smile
  /* EXCITED  */ {  0, 30, 30, 3,   0,  0,  0, 10,  0, 30, -8, 0, 3 },  // ∪ enthusiastic smile
  /* READY    */ {  0, 30, 30, 3,   0,  0,  0, 10,  0, 26, -3, 0, 3 },  // ∪ calmer smile
  /* WAKING   */ { -2, 34, 34, 3,   0,  0,  0, 12,  0, 14,  0, 9, 3 },
  /* SLEEP    */ {  8, 26,  2, 3,   0,  0,  0,  0,  0, 18,  0, 0, 3 },
};

// Tween duration between state targets. Feels snappy without snapping.
static constexpr uint32_t kTweenMs       = 250;
// Render rate cap. 30 fps is plenty for this palette and gives headroom
// for the rest of the loop (WS, motion, personality).
static constexpr uint32_t kTickIntervalMs = 33;

static constexpr uint32_t kBlinkCloseMs = 80;
static constexpr uint32_t kBlinkOpenMs  = 130;

// ---- Running state --------------------------------------------------------

static FaceParams sFrom;
static FaceParams sTo;
static uint32_t   sTweenStartMs = 0;
static Personality::State sLastState = Personality::kStateCount;

static uint32_t sNextBlinkMs  = 0;   // 0 when none scheduled
static uint32_t sBlinkStartMs = 0;
static bool     sBlinkActive  = false;

static uint32_t sLastTickMs = 0;

// ---- Math helpers --------------------------------------------------------

static int16_t lerpi(int16_t a, int16_t b, float t) {
  return (int16_t)(a + (b - a) * t);
}

static float clamp01(float t) {
  return t < 0 ? 0 : (t > 1 ? 1 : t);
}

static float smoothstep01(float t) {
  t = clamp01(t);
  return t * t * (3 - 2 * t);
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
  return r;
}

// ---- Modulators ---------------------------------------------------------

// Breath: ±1.5 px sinusoidal over 4s. Suppressed during FINISHED (the
// celebration speaks for itself) and SLEEP (we use a slower breath baked
// into eye_dy instead, if we want — currently none).
static float breathPhase(uint32_t now) {
  const float t = (float)(now % 4000) / 4000.0f;
  return sinf(t * 2.0f * (float)PI);
}

// Gaze wander amplitudes per state. Centre of gaze stays at pupil_dx/dy
// from the params; modulator adds on top.
static void gazeFor(Personality::State s, uint32_t now,
                    int16_t& gdx, int16_t& gdy) {
  gdx = 0; gdy = 0;
  switch (s) {
    case Personality::IDLE: {
      const float t1 = (float)(now % 7000) / 7000.0f;
      const float t2 = (float)(now % 5000) / 5000.0f;
      gdx = (int16_t)(sinf(t1 * 2 * (float)PI) * 4);
      gdy = (int16_t)(sinf(t2 * 2 * (float)PI) * 2);
      break;
    }
    case Personality::THINKING: {
      // Small tight circle — "eyes darting in thought"
      const float t = (float)(now % 900) / 900.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 2);
      gdy = (int16_t)(cosf(t * 2 * (float)PI) * 2);
      break;
    }
    case Personality::READING: {
      // Fast horizontal scan — "reading left to right"
      const float t = (float)(now % 1300) / 1300.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 6);
      break;
    }
    case Personality::WRITING: {
      // Tiny drift — pupils mostly fixed on the "work"
      const float t = (float)(now % 2200) / 2200.0f;
      gdx = (int16_t)(sinf(t * 2 * (float)PI) * 2);
      break;
    }
    case Personality::EXCITED: {
      // Bright, slightly bouncier wander than ready
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
  // 0 = blinks suppressed in this state.
  switch (s) {
    case Personality::IDLE:     return (uint32_t)random(4000, 6500);
    case Personality::THINKING: return (uint32_t)random(2000, 3500);
    case Personality::READING:  return (uint32_t)random(4000, 6000);
    case Personality::WRITING:  return (uint32_t)random(3500, 5500);
    case Personality::EXCITED:  return (uint32_t)random(2500, 4000);
    case Personality::READY:    return (uint32_t)random(3000, 4500);
    default:                    return 0;
  }
}

static void scheduleNextBlink(Personality::State s, uint32_t from) {
  const uint32_t p = blinkPeriodMsFor(s);
  sNextBlinkMs = (p == 0) ? 0 : from + p;
}

// 0 = fully open, 1 = fully closed. Clears sBlinkActive when done.
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

// ---- Rendering primitives -----------------------------------------------

// Parabolic stroke. `bend > 0` → middle peaks up (∩), `bend < 0` → middle
// sags down (∪), 0 → flat. Draws a single-pixel-wide column per x with
// `thick` vertical pixels.
static void drawParabola(TFT_eSprite& s, int16_t cx, int16_t cy,
                         int16_t w, int16_t bend, int16_t thick) {
  if (w < 2) return;
  if (thick < 1) thick = 1;
  const int16_t halfw = w / 2;
  for (int16_t dx = -halfw; dx <= halfw; ++dx) {
    const float norm = (float)dx / (float)halfw;
    const float f = 1.0f - norm * norm;
    const int16_t dy = (int16_t)(-bend * f);  // screen y: up = smaller
    s.fillRect(cx + dx, cy + dy - thick / 2, 1, thick, kFg);
  }
}

// One eye. Decides between "ring with pupil" and "parabolic stroke" based
// on current ry and eye_curve.
static void drawEye(TFT_eSprite& s, const FaceParams& p,
                    int16_t cx, int16_t cy, float blinkAmt,
                    int16_t gdx, int16_t gdy) {
  // Blink closes the eye by collapsing ry toward ~2 (keep a visible bar).
  int16_t ry = p.eye_ry;
  if (blinkAmt > 0.01f) {
    const float k = clamp01(blinkAmt);
    ry = (int16_t)(ry * (1.0f - k) + 2 * k);
    if (ry < 2) ry = 2;
  }
  const int16_t rx = p.eye_rx;

  // Stroke mode: happy/sad arcs (eye_curve != 0) or very-closed eye.
  if (p.eye_curve != 0 || ry < 5) {
    drawParabola(s, cx, cy, rx * 2, p.eye_curve, p.eye_stroke);
    return;
  }

  // Ring: outer filled white ellipse, inner filled black ellipse, pupil.
  s.fillEllipse(cx, cy, rx, ry, kFg);
  const int16_t irx = rx - p.eye_stroke;
  const int16_t iry = ry - p.eye_stroke;
  if (irx > 0 && iry > 0) {
    s.fillEllipse(cx, cy, irx, iry, kBg);
  }
  if (p.pupil_r > 0 && blinkAmt < 0.6f) {
    s.fillSmoothCircle(cx + p.pupil_dx + gdx,
                       cy + p.pupil_dy + gdy,
                       p.pupil_r, kFg, kBg);
  }
}

static void drawMouth(TFT_eSprite& s, const FaceParams& p) {
  const int16_t cy = kMouthY + p.mouth_dy;
  if (p.mouth_open_h > 0) {
    s.fillEllipse(kCx, cy, p.mouth_w / 2, p.mouth_open_h, kFg);
    return;
  }
  drawParabola(s, kCx, cy, p.mouth_w, p.mouth_curve, p.mouth_thick);
}

static void renderFrame(TFT_eSprite& s, const FaceParams& p,
                        float blinkAmt, int16_t gdx, int16_t gdy) {
  s.fillSprite(kBg);
  const int16_t ey = kEyeY + p.eye_dy;
  drawEye(s, p, kEyeLX, ey, blinkAmt, gdx, gdy);
  drawEye(s, p, kEyeRX, ey, blinkAmt, gdx, gdy);
  drawMouth(s, p);
}

// ---- Lifecycle ---------------------------------------------------------

void begin() {
  // Seed the Arduino PRNG from the ESP32 hardware RNG so blink/gaze
  // patterns aren't identical across boots.
  randomSeed(esp_random());

  sLastState    = Personality::kStateCount;
  // Seed the initial frame to match the boot personality state so we
  // don't tween from idle → sleep (eyes opening then closing) on boot.
  sFrom         = kTargets[Personality::SLEEP];
  sTo           = sFrom;
  sTweenStartMs = millis();
  sNextBlinkMs  = 0;
  sBlinkActive  = false;
  sLastTickMs   = 0;
}

void invalidate() {
  // Force the next tick to redraw even if the 33ms gate hasn't elapsed.
  sLastTickMs = 0;
}

static void onStateChange(Personality::State newState, uint32_t now) {
  // Freeze the current interpolated frame as the new "from" so we tween
  // from wherever we actually are, not from sTo. Prevents snap-back when
  // a state change interrupts a tween.
  float t = (float)(now - sTweenStartMs) / (float)kTweenMs;
  FaceParams currentFrame = lerpParams(sFrom, sTo, smoothstep01(t));
  sFrom         = currentFrame;
  sTo           = kTargets[newState];
  sTweenStartMs = now;
  sLastState    = newState;

  sBlinkActive = false;
  scheduleNextBlink(newState, now);
}

void tick() {
  if (!Display::ready()) return;

  const uint32_t now = millis();
  if (now - sLastTickMs < kTickIntervalMs) return;
  sLastTickMs = now;

  const Personality::State s = Personality::current();
  if (s != sLastState) {
    onStateChange(s, now);
  }

  // Tween progress (clamped + smoothed)
  const float t  = (float)(now - sTweenStartMs) / (float)kTweenMs;
  const float te = smoothstep01(t);
  FaceParams p   = lerpParams(sFrom, sTo, te);

  // Breath: subtle sinusoidal ±1-2 px on eyes + mouth.
  if (s != Personality::FINISHED && s != Personality::SLEEP) {
    const int16_t b = (int16_t)(breathPhase(now) * 1.5f);
    p.eye_dy   = (int16_t)(p.eye_dy   + b);
    p.mouth_dy = (int16_t)(p.mouth_dy + b / 2);
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
  renderFrame(spr, p, blinkAmt, gdx, gdy);
  Display::pushFrame();
}

}  // namespace Face
