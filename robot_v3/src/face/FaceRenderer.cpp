#include "FaceRenderer.h"

#include <math.h>

namespace Face {

// Semicircular interp between apex (n=0) and corner (|n|=1):
//   y(n) = corner + (apex - corner) * sqrt(1 - n^2)
// Top and bottom edges with mirrored apexes about y=0 trace a perfect
// ellipse, so `eye_top_apex = -ry, eye_bot_apex = +ry` gives a circular
// eye when rx == ry.
static inline float curveAt(int16_t apex, int16_t corner, float n) {
  const float r = sqrtf(fmaxf(0.0f, 1.0f - n * n));
  return (float)corner + ((float)apex - (float)corner) * r;
}

static inline float wavePhaseRad(int16_t speed_deg_per_sec, uint32_t nowMs) {
  // (speed * t_sec) deg → rad. Keep in float; phase wraps naturally inside sinf.
  return (float)speed_deg_per_sec * (float)nowMs * (float)(M_PI / 180000.0);
}

// Paint a vertical span [ly0..ly1] in shape-local coords as a single rotated
// line in screen coords. Caller guarantees ly1 >= ly0.
static inline void paintLocalSpan(TFT_eSprite& s, int16_t cx, int16_t cy, float fx, float ly0,
                                  float ly1, float cosA, float sinA, uint16_t color) {
  const float ax = fx * cosA;
  const float ay = fx * sinA;
  const int16_t x0 = cx + (int16_t)lroundf(ax - ly0 * sinA);
  const int16_t y0 = cy + (int16_t)lroundf(ay + ly0 * cosA);
  const int16_t x1 = cx + (int16_t)lroundf(ax - ly1 * sinA);
  const int16_t y1 = cy + (int16_t)lroundf(ay + ly1 * cosA);
  s.drawLine(x0, y0, x1, y1, color);

  // Rotated spans can leave 1px seams between adjacent columns due to endpoint
  // rounding. Stamp a second nearby line only when meaningfully rotated.
  const float rotMix = fabsf(sinA * cosA);
  if (rotMix > 0.08f) {
    const int16_t dx = (int16_t)(x1 - x0);
    const int16_t dy = (int16_t)(y1 - y0);
    if (abs(dx) >= abs(dy)) {
      s.drawLine(x0, (int16_t)(y0 + 1), x1, (int16_t)(y1 + 1), color);
    } else {
      s.drawLine((int16_t)(x0 + 1), y0, (int16_t)(x1 + 1), y1, color);
    }
  }
}

static inline void localToScreen(float lx, float ly, int16_t cx, int16_t cy, float cosA, float sinA,
                                 int16_t* outX, int16_t* outY) {
  if (!outX || !outY) return;
  *outX = cx + (int16_t)lroundf(lx * cosA - ly * sinA);
  *outY = cy + (int16_t)lroundf(lx * sinA + ly * cosA);
}

// Trace an elliptical arc as a polyline, then expand outward by 1px and
// re-trace, repeating `thick` times so the resulting band has uniform
// thickness perpendicular to the arc — including at the corners where
// the curve's tangent goes vertical (which a column-major stroke can't
// do because its pixels travel parallel to the tangent there).
//
// The k=0 layer matches the user-visible envelope (halfw, apex, corner).
// Layer k expands rx by k and pushes apex away from corner by k in the
// `outwardSign` direction; corner stays put. The wave shifts every layer
// by the same amount at corresponding fractional position, so all layers
// move together.
static void drawEdgeStroke(TFT_eSprite& s, int16_t cx, int16_t cy,
                           int16_t halfw, int16_t apex, int16_t corner,
                           float blinkScale, int16_t thick, int outwardSign,
                           float waveAmp, float waveFreq, float wavePhase,
                           float cosA, float sinA, uint16_t color) {
  if (halfw < 1 || thick < 1) return;
  for (int16_t k = 0; k < thick; ++k) {
    const int16_t rxk = halfw + k;
    const float apexK = (float)apex + (float)(outwardSign * k);
    int16_t prevPx = 0, prevPy = 0;
    bool havePrev = false;
    for (int16_t lx = -rxk; lx <= rxk; ++lx) {
      const float n = (float)lx / (float)rxk;
      const float r = sqrtf(fmaxf(0.0f, 1.0f - n * n));
      float ly = ((float)corner + (apexK - (float)corner) * r) * blinkScale;
      if (waveAmp != 0.0f) {
        // Wave argument uses the layer's own n so phase stays smooth across layers.
        ly += waveAmp * sinf(2.0f * (float)M_PI * waveFreq * n + wavePhase);
      }
      const int16_t px = cx + (int16_t)lroundf((float)lx * cosA - ly * sinA);
      const int16_t py = cy + (int16_t)lroundf((float)lx * sinA + ly * cosA);
      if (havePrev) {
        s.drawLine(prevPx, prevPy, px, py, color);
      }
      prevPx = px;
      prevPy = py;
      havePrev = true;
    }
  }
}

static void drawMouth(TFT_eSprite& s, const FaceParams& p, int16_t cx, int16_t cy, uint32_t nowMs,
                      float cosA, float sinA, uint16_t fg565) {
  const int16_t halfw = p.mouth_rx;
  if (halfw < 1) return;

  const float wavePhase = wavePhaseRad(p.mouth_wave_speed, nowMs);
  // Scale by 1/50 so a wave_freq slider value of 50 ≈ 1 cycle across the shape.
  const float waveFreq = (float)p.mouth_wave_freq * 0.02f;
  const float waveAmp = (float)p.mouth_wave_amp;
  const float minThick = (float)p.mouth_thick;

  for (int16_t lx = -halfw; lx <= halfw; ++lx) {
    const float n = (float)lx / (float)halfw;
    float yt = curveAt(p.mouth_top_apex, p.mouth_top_corner, n);
    float yb = curveAt(p.mouth_bot_apex, p.mouth_bot_corner, n);
    if (waveAmp != 0.0f) {
      const float w = waveAmp * sinf(2.0f * (float)M_PI * waveFreq * n + wavePhase);
      yt += w;
      yb += w;
    }
    if (yb < yt) {
      const float tmp = yt;
      yt = yb;
      yb = tmp;
    }
    if ((yb - yt) < minThick) {
      const float mid = 0.5f * (yt + yb);
      yt = mid - 0.5f * minThick;
      yb = mid + 0.5f * minThick;
    }
    paintLocalSpan(s, cx, cy, (float)lx, yt, yb, cosA, sinA, fg565);
  }
}

static void drawEye(TFT_eSprite& s, const FaceParams& p, int16_t cx, int16_t cy, float blinkAmt,
                    int16_t gdx, int16_t gdy, uint32_t nowMs, float cosA, float sinA,
                    uint16_t fg565, uint16_t bg565) {
  const int16_t halfw = p.eye_rx;
  if (halfw < 1) return;

  // Blink squeezes the envelope vertically toward y=0. blinkAmt=1 collapses to zero gap.
  const float blink = clamp01(blinkAmt);
  const float blinkScale = 1.0f - blink;

  const float wavePhase = wavePhaseRad(p.eye_wave_speed, nowMs);
  const float waveFreq = (float)p.eye_wave_freq * 0.02f;
  const float waveAmp = (float)p.eye_wave_amp;

  // Pupil position in eye-local coords.
  const float pupilLx = (float)(p.pupil_dx + gdx);
  const float pupilLy = (float)(p.pupil_dy + gdy);
  const float pupilR = (float)p.pupil_r;
  const float pupilR2 = pupilR * pupilR;
  const float maskPupilR = pupilR + 2.0f;
  const float maskPupilR2 = maskPupilR * maskPupilR;
  const bool drawPupil = (p.pupil_r > 0) && (blink < 0.6f);
  const int16_t pupilMinX = (int16_t)floorf(pupilLx - maskPupilR) - 1;
  const int16_t pupilMaxX = (int16_t)ceilf(pupilLx + maskPupilR) + 1;

  if (drawPupil) {
    int16_t px = 0;
    int16_t py = 0;
    localToScreen(pupilLx, pupilLy, cx, cy, cosA, sinA, &px, &py);
    // Draw anti-aliased pupil first; edge pass below trims overflow.
    s.fillSmoothCircle(px, py, (int32_t)lroundf(pupilR), fg565, bg565);
  }

  // --- Interior fill (column-major, hollow inside the inner envelope) ---
  for (int16_t lx = -halfw; lx <= halfw; ++lx) {
    const float n = (float)lx / (float)halfw;
    float yt = curveAt(p.eye_top_apex, p.eye_top_corner, n) * blinkScale;
    float yb = curveAt(p.eye_bot_apex, p.eye_bot_corner, n) * blinkScale;
    if (waveAmp != 0.0f) {
      const float w = waveAmp * sinf(2.0f * (float)M_PI * waveFreq * n + wavePhase);
      yt += w;
      yb += w;
    }
    if (yb < yt) {
      const float tmp = yt;
      yt = yb;
      yb = tmp;
    }
    const float clipTopBound = yt;
    const float clipBotBound = yb > yt ? yb : yt;

    // Pupil is pre-rendered as a smooth circle. Mask only the overhang that
    // falls outside the eye envelope with bg-colored spans.
    if (drawPupil && lx >= pupilMinX && lx <= pupilMaxX) {
      const float dx = (float)lx - pupilLx;
      if (dx * dx <= maskPupilR2) {
        const float dyMag = sqrtf(maskPupilR2 - dx * dx);
        const float pupilTop = pupilLy - dyMag;
        const float pupilBot = pupilLy + dyMag;
        if (pupilTop < clipTopBound) {
          const float maskBot = pupilBot < clipTopBound ? pupilBot : clipTopBound;
          if (maskBot > pupilTop) {
            paintLocalSpan(s, cx, cy, (float)lx, pupilTop, maskBot, cosA, sinA, bg565);
          }
        }
        if (pupilBot > clipBotBound) {
          const float maskTop = pupilTop > clipBotBound ? pupilTop : clipBotBound;
          if (pupilBot > maskTop) {
            paintLocalSpan(s, cx, cy, (float)lx, maskTop, pupilBot, cosA, sinA, bg565);
          }
        }
      }
    }
  }

  // If the pupil extends beyond the eye width, clip the side overhang with
  // bg-colored "rectangles" (column spans) in local space.
  if (drawPupil) {
    const float sideTop = pupilLy - maskPupilR;
    const float sideBot = pupilLy + maskPupilR;

    if (pupilMinX < -halfw) {
      const int16_t leftEnd = pupilMaxX < (int16_t)(-halfw - 1) ? pupilMaxX : (int16_t)(-halfw - 1);
      for (int16_t lx = pupilMinX; lx <= leftEnd; ++lx) {
        paintLocalSpan(s, cx, cy, (float)lx, sideTop, sideBot, cosA, sinA, bg565);
      }
    }
    if (pupilMaxX > halfw) {
      const int16_t rightStart = pupilMinX > (int16_t)(halfw + 1) ? pupilMinX : (int16_t)(halfw + 1);
      for (int16_t lx = rightStart; lx <= pupilMaxX; ++lx) {
        paintLocalSpan(s, cx, cy, (float)lx, sideTop, sideBot, cosA, sinA, bg565);
      }
    }
  }

  // --- Outward strokes: concentric arc layers, top edge then bot edge ---
  const int16_t thick = p.eye_thick > 0 ? p.eye_thick : 1;
  drawEdgeStroke(s, cx, cy, halfw, p.eye_top_apex, p.eye_top_corner,
                 blinkScale, thick, /*outwardSign=*/-1,
                 waveAmp, waveFreq, wavePhase, cosA, sinA, fg565);
  drawEdgeStroke(s, cx, cy, halfw, p.eye_bot_apex, p.eye_bot_corner,
                 blinkScale, thick, /*outwardSign=*/+1,
                 waveAmp, waveFreq, wavePhase, cosA, sinA, fg565);
}

void drawFace(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx, int16_t gdy,
              Expression /*expr*/, uint32_t nowMs, uint16_t fg565, uint16_t bg565) {
  const float angleRad = (float)p.face_rot * (float)M_PI / 180.0f;
  const float cosA = cosf(angleRad);
  const float sinA = sinf(angleRad);

  const int16_t shorten = (int16_t)(abs(p.face_y) / 2);
  const auto compress = [&](int16_t fy) -> int16_t {
    const int16_t dy = (int16_t)(fy - kPivotY);
    if (dy > 0) {
      const int16_t nd = (int16_t)(dy - shorten);
      return (int16_t)(kPivotY + (nd > 0 ? nd : 0));
    }
    if (dy < 0) {
      const int16_t nd = (int16_t)(-dy - shorten);
      return (int16_t)(kPivotY - (nd > 0 ? nd : 0));
    }
    return fy;
  };

  const auto rotated = [&](int16_t fx, int16_t fy, int16_t& outx, int16_t& outy) {
    const float dx = (float)(fx - kCx);
    const float dy = (float)(fy - kPivotY);
    outx = kCx + (int16_t)(dx * cosA - dy * sinA);
    outy = kPivotY + (int16_t)(dx * sinA + dy * cosA) + p.face_y;
  };

  int16_t lex, ley, rex, rey, mx, my;
  rotated(kEyeLX, compress(kEyeY + p.eye_dy), lex, ley);
  rotated(kEyeRX, compress(kEyeY + p.eye_dy), rex, rey);
  rotated(kCx, compress(kMouthY + p.mouth_dy), mx, my);

  drawEye(s, p, lex, ley, blinkAmt, gdx, gdy, nowMs, cosA, sinA, fg565, bg565);
  drawEye(s, p, rex, rey, blinkAmt, gdx, gdy, nowMs, cosA, sinA, fg565, bg565);
  drawMouth(s, p, mx, my, nowMs, cosA, sinA, fg565);
}

}  // namespace Face
