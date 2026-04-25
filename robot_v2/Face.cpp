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
// breath, gaze wander, thinking tilt-flip) layer on top.

struct FaceParams {
  int16_t eye_dy;        // vertical offset of both eyes from baseline (face-local)
  int16_t eye_rx;        // eye ellipse half-width
  int16_t eye_ry;        // eye ellipse half-height
  int16_t eye_stroke;    // ring outline thickness
  int16_t eye_curve;     // !=0 → render as parabolic stroke; +=∩ (happy), −=∪ (sad)
  int16_t pupil_dx;      // pupil offset in face-local coords (rotated with face)
  int16_t pupil_dy;
  int16_t pupil_r;       // 0 = no pupil
  int16_t mouth_dy;
  int16_t mouth_w;
  int16_t mouth_curve;   // parabola bend: +=frown (∩), −=smile (∪), 0=flat
  int16_t mouth_open_h;  // if >0, mouth renders as filled oval of this half-height
  int16_t mouth_thick;
  int16_t face_rot;      // whole-face rotation in degrees, +=clockwise on screen
};

// Baseline geometry
static constexpr int16_t kCx      = 120;
static constexpr int16_t kEyeY    = 95;
static constexpr int16_t kEyeLX   = 85;
static constexpr int16_t kEyeRX   = 155;
static constexpr int16_t kMouthY  = 165;
static constexpr int16_t kPivotY  = 130;   // face rotation pivot (between eyes + mouth)

static constexpr uint16_t kFg = TFT_WHITE;
static constexpr uint16_t kBg = TFT_BLACK;

// Per-state target params. Row order must match Personality::State.
//
// Curve convention (matches drawParabola):
//   bend > 0 → ∩ peak shape (happy eyes; frown mouth)
//   bend < 0 → ∪ sag shape  (sad eyes; smile mouth)
//
// face_rot represents the "default" tilt (sign = +1). The thinking
// modulator periodically flips this sign so the tilt swaps direction.
static const FaceParams kTargets[Personality::kStateCount] = {
  /* IDLE     */ {  2, 30, 22, 3,   0,  0,  3, 15,  0, 30,   1,  0, 3,   0 },
  /* THINKING */ {  0, 30, 30, 3,   0,  7, -9, 15,  0, 22,  -3,  0, 3, -10 },  // tilted left, lil smile
  /* READING  */ {  0, 32, 16, 3,   0,  0,  0, 12,  0, 18,   0,  0, 3,   0 },
  /* WRITING  */ {  0, 30, 26, 3,   0,  0,  8, 15,  0, 16,   0,  5, 3,   0 },
  /* FINISHED */ { -4, 24,  4, 4,   7,  0,  0,  0,  0, 36,  -1, 14, 4,   0 },  // shallower ∩, D smile
  /* EXCITED  */ {  0, 30, 30, 3,   0,  0,  0, 15,  0, 30,  -8,  0, 3,   0 },
  /* READY    */ {  0, 30, 30, 3,   0,  0,  0, 15,  0, 26,  -3,  0, 3,   0 },
  /* WAKING   */ { -2, 34, 34, 3,   0,  0,  0, 18,  0, 14,   0,  9, 3,   0 },
  /* SLEEP    */ {  8, 26,  2, 3,   0,  0,  0,  0,  0, 18,   0,  0, 3,   0 },
};

// Tween duration between state targets.
static constexpr uint32_t kTweenMs        = 250;
// Render rate cap (~30 fps).
static constexpr uint32_t kTickIntervalMs = 33;

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

// Thinking tilt-flip state.
static float    sThinkFromSign     = 1.0f;
static float    sThinkToSign       = 1.0f;
static uint32_t sThinkFlipStartMs  = 0;
static uint32_t sNextThinkFlipMs   = 0;

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
  r.face_rot     = lerpi(a.face_rot,     b.face_rot,     t);
  return r;
}

// ---- Modulators ---------------------------------------------------------

static float breathPhase(uint32_t now) {
  const float t = (float)(now % 4000) / 4000.0f;
  return sinf(t * 2.0f * (float)PI);
}

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

// ---- Rendering primitives -----------------------------------------------

// Parabolic stroke, optionally rotated. Draws a thick vertical column at
// each rotated x. For small rotations (≤15°) the vertical-bar thickness
// is visually fine; for large rotations the columns would need to be
// perpendicular to the rotated baseline, but we don't drive face_rot
// that far.
static void drawParabola(TFT_eSprite& s, int16_t cx, int16_t cy,
                         int16_t w, int16_t bend, int16_t thick,
                         float cosA, float sinA) {
  if (w < 2) return;
  if (thick < 1) thick = 1;
  const int16_t halfw = w / 2;
  for (int16_t lx = -halfw; lx <= halfw; ++lx) {
    const float norm = (float)lx / (float)halfw;
    const float ly = -bend * (1.0f - norm * norm);
    const float rx = (float)lx * cosA - ly * sinA;
    const float ry = (float)lx * sinA + ly * cosA;
    const int16_t px = cx + (int16_t)rx;
    const int16_t py = cy + (int16_t)ry;
    s.fillRect(px, py - thick / 2, 1, thick, kFg);
  }
}

static void drawEye(TFT_eSprite& s, const FaceParams& p,
                    int16_t cx, int16_t cy, float blinkAmt,
                    int16_t gdx, int16_t gdy,
                    float cosA, float sinA) {
  int16_t ry = p.eye_ry;
  if (blinkAmt > 0.01f) {
    const float k = clamp01(blinkAmt);
    ry = (int16_t)(ry * (1.0f - k) + 2 * k);
    if (ry < 2) ry = 2;
  }
  const int16_t rx = p.eye_rx;

  if (p.eye_curve != 0 || ry < 5) {
    // Closed-eye / arc rendering: double the stroke. Applies to sleep
    // (closed line), finished (∩ happy arcs), and any state mid-blink.
    drawParabola(s, cx, cy, rx * 2, p.eye_curve, p.eye_stroke * 2,
                 cosA, sinA);
    return;
  }

  // Eye ellipse stays axis-aligned (acceptable for ≤15° tilts and avoids
  // an aliased rotated-ellipse renderer). Pupil offset rotates with the
  // face so the gaze direction stays correct relative to the head.
  s.fillEllipse(cx, cy, rx, ry, kFg);
  const int16_t irx = rx - p.eye_stroke;
  const int16_t iry = ry - p.eye_stroke;
  if (irx > 0 && iry > 0) {
    s.fillEllipse(cx, cy, irx, iry, kBg);
  }
  if (p.pupil_r > 0 && blinkAmt < 0.6f) {
    const float ldx = (float)(p.pupil_dx + gdx);
    const float ldy = (float)(p.pupil_dy + gdy);
    const float rdx = ldx * cosA - ldy * sinA;
    const float rdy = ldx * sinA + ldy * cosA;

    // Clamp pupil radius so it stays inside the inner (black) ellipse —
    // otherwise large pupils bleed onto the white ring or beyond,
    // especially when the eye is narrowed (idle, reading) or the gaze
    // is offset toward an edge (thinking, writing). Bounding-box clip
    // is conservative but cheap; the pupil shrinks as it approaches an
    // eyelid, which actually reads as a cute "lid covers pupil" effect.
    const float slackX = (float)irx - fabsf(rdx);
    const float slackY = (float)iry - fabsf(rdy);
    int16_t maxR = (int16_t)fminf(slackX, slackY);
    if (maxR < 1) return;
    int16_t effR = p.pupil_r;
    if (effR > maxR) effR = maxR;

    s.fillSmoothCircle(cx + (int16_t)rdx, cy + (int16_t)rdy,
                       effR, kFg, kBg);
  }
}

// Half-ellipse with a flat top — the "D rotated 90°" shape for a happy
// open-mouthed smile. Filled column-by-column; for non-zero rotation each
// column is drawn as a rotated line segment. Suitable for the small
// face-tilt magnitudes we use (≤15°).
static void drawHalfEllipse(TFT_eSprite& s, int16_t cx, int16_t cy,
                            int16_t rx, int16_t ry,
                            float cosA, float sinA) {
  if (rx < 1 || ry < 1) return;
  for (int16_t lx = -rx; lx <= rx; ++lx) {
    const float norm = (float)lx / (float)rx;
    const float h = (float)ry * sqrtf(fmaxf(0.0f, 1.0f - norm * norm));
    if (h < 0.5f) continue;
    // Column goes from face-local (lx, 0) — the flat top — down to (lx, h).
    const float top_rx = (float)lx * cosA;
    const float top_ry = (float)lx * sinA;
    const float bot_rx = (float)lx * cosA - h * sinA;
    const float bot_ry = (float)lx * sinA + h * cosA;
    s.drawLine(cx + (int16_t)top_rx, cy + (int16_t)top_ry,
               cx + (int16_t)bot_rx, cy + (int16_t)bot_ry, kFg);
  }
}

static void drawMouth(TFT_eSprite& s, const FaceParams& p,
                      int16_t cx, int16_t cy,
                      float cosA, float sinA) {
  if (p.mouth_open_h > 0) {
    if (p.mouth_curve < 0) {
      // D-shape: open smile with flat top, curved bottom.
      drawHalfEllipse(s, cx, cy, p.mouth_w / 2, p.mouth_open_h, cosA, sinA);
    } else {
      // Full oval ("oh" mouth — waking).
      s.fillEllipse(cx, cy, p.mouth_w / 2, p.mouth_open_h, kFg);
    }
    return;
  }
  drawParabola(s, cx, cy, p.mouth_w, p.mouth_curve, p.mouth_thick,
               cosA, sinA);
}

static void renderFrame(TFT_eSprite& s, const FaceParams& p,
                        float blinkAmt, int16_t gdx, int16_t gdy) {
  s.fillSprite(kBg);

  const float angleRad = (float)p.face_rot * (float)PI / 180.0f;
  const float cosA = cosf(angleRad);
  const float sinA = sinf(angleRad);

  // Rotate face-local element positions around the pivot.
  const auto rotated = [&](int16_t fx, int16_t fy,
                           int16_t& outx, int16_t& outy) {
    const float dx = (float)(fx - kCx);
    const float dy = (float)(fy - kPivotY);
    outx = kCx     + (int16_t)(dx * cosA - dy * sinA);
    outy = kPivotY + (int16_t)(dx * sinA + dy * cosA);
  };

  int16_t lex, ley, rex, rey, mx, my;
  rotated(kEyeLX, kEyeY   + p.eye_dy,   lex, ley);
  rotated(kEyeRX, kEyeY   + p.eye_dy,   rex, rey);
  rotated(kCx,    kMouthY + p.mouth_dy, mx,  my);

  drawEye(s, p, lex, ley, blinkAmt, gdx, gdy, cosA, sinA);
  drawEye(s, p, rex, rey, blinkAmt, gdx, gdy, cosA, sinA);
  drawMouth(s, p, mx, my, cosA, sinA);
}

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
  sLastState    = newState;

  sBlinkActive = false;
  scheduleNextBlink(newState, now);

  if (newState == Personality::THINKING) {
    resetThinkTilt(now);
  }
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

  // Tween progress
  const float t  = (float)(now - sTweenStartMs) / (float)kTweenMs;
  const float te = smoothstep01(t);
  FaceParams p   = lerpParams(sFrom, sTo, te);

  // Breath
  if (s != Personality::FINISHED && s != Personality::SLEEP) {
    const int16_t b = (int16_t)(breathPhase(now) * 1.5f);
    p.eye_dy   = (int16_t)(p.eye_dy   + b);
    p.mouth_dy = (int16_t)(p.mouth_dy + b / 2);
  }

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
  renderFrame(spr, p, blinkAmt, gdx, gdy);
  Display::pushFrame();
}

}  // namespace Face
