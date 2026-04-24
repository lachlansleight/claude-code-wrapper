#include "Face.h"

#include <TFT_eSPI.h>

#include "Display.h"
#include "Personality.h"

namespace Face {

// ---- Geometry -------------------------------------------------------------

static constexpr int16_t kW  = 240;
static constexpr int16_t kH  = 240;
static constexpr int16_t kCx = 120;

// Eyes sit above the centreline; mouth below. Eyes are placed ~35px
// either side of centre (70px apart centre-to-centre) at y=95, which
// reads as a cute high-forehead face on a 240x240 panel.
static constexpr int16_t kEyeY       = 95;
static constexpr int16_t kEyeLX      = 85;
static constexpr int16_t kEyeRX      = 155;
static constexpr int16_t kEyeR       = 30;
static constexpr int16_t kEyeStroke  = 3;
static constexpr int16_t kPupilR     = 10;

static constexpr int16_t kMouthY     = 165;
static constexpr int16_t kMouthThick = 3;

static constexpr uint16_t kFg = TFT_WHITE;
static constexpr uint16_t kBg = TFT_BLACK;

// ---- Primitives ----------------------------------------------------------

// Ring-outline eye with solid pupil at (cx+pdx, cy+pdy). The ring is a
// white disc with a black disc punched out of its centre, so antialiasing
// has the right background to blend against on each pass.
static void drawEye(TFT_eSprite& s, int16_t cx, int16_t cy,
                    int16_t pdx, int16_t pdy) {
  s.fillSmoothCircle(cx, cy, kEyeR,               kFg, kBg);
  s.fillSmoothCircle(cx, cy, kEyeR - kEyeStroke,  kBg, kFg);
  s.fillSmoothCircle(cx + pdx, cy + pdy, kPupilR, kFg, kBg);
}

static void drawFlatMouth(TFT_eSprite& s, int16_t width) {
  s.fillRect(kCx - width / 2, kMouthY - kMouthThick / 2,
             width, kMouthThick, kFg);
}

// ---- Per-state frames ----------------------------------------------------
//
// Only IDLE + THINKING are authored for v1. The others fall through to
// a placeholder so the state machine can visibly drive the panel even
// before those frames are written.

static void drawIdle(TFT_eSprite& s) {
  drawEye(s, kEyeLX, kEyeY, 0, 0);
  drawEye(s, kEyeRX, kEyeY, 0, 0);
  drawFlatMouth(s, 32);
}

static void drawThinking(TFT_eSprite& s) {
  // Pupils drift up-and-to-the-right — classic "thinking" look-away.
  drawEye(s, kEyeLX, kEyeY,  6, -8);
  drawEye(s, kEyeRX, kEyeY,  6, -8);
  drawFlatMouth(s, 22);
}

// Placeholder frame for states we haven't authored yet: shows eyes
// (so the face is recognisable) plus the state name as a small label.
// Lets us see the state machine progressing through reading / writing /
// etc. before we commit to a final look.
static void drawPlaceholder(TFT_eSprite& s, Personality::State st) {
  drawEye(s, kEyeLX, kEyeY, 0, 0);
  drawEye(s, kEyeRX, kEyeY, 0, 0);
  s.setTextDatum(MC_DATUM);
  s.setTextColor(kFg, kBg);
  s.setTextSize(2);
  s.drawString(Personality::stateName(st), kCx, kMouthY + 10);
  s.setTextDatum(TL_DATUM);
}

static void renderState(TFT_eSprite& s, Personality::State st) {
  switch (st) {
    case Personality::IDLE:     drawIdle(s);         break;
    case Personality::THINKING: drawThinking(s);     break;
    default:                    drawPlaceholder(s, st); break;
  }
}

// ---- Tick / redraw ------------------------------------------------------

static Personality::State sLastDrawn = Personality::kStateCount;  // force first draw
static bool               sDirty     = true;

void invalidate() { sDirty = true; }

void begin() {
  sLastDrawn = Personality::kStateCount;
  sDirty     = true;
}

void tick() {
  if (!Display::ready()) return;

  const Personality::State st = Personality::current();
  if (!sDirty && st == sLastDrawn) return;

  TFT_eSprite& s = Display::sprite();
  s.fillSprite(kBg);
  renderState(s, st);
  Display::pushFrame();

  sLastDrawn = st;
  sDirty     = false;
}

}  // namespace Face
