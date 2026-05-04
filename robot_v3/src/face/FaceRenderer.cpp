#include "FaceRenderer.h"

#include <math.h>

namespace Face {

static void drawParabola(TFT_eSprite& s, int16_t cx, int16_t cy, int16_t w, int16_t bend,
                         int16_t thick, float cosA, float sinA, uint16_t fg565) {
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
    s.fillRect(px, py - thick / 2, 1, thick, fg565);
  }
}

static void drawEye(TFT_eSprite& s, const FaceParams& p, int16_t cx, int16_t cy, float blinkAmt,
                    int16_t gdx, int16_t gdy, float cosA, float sinA, uint16_t fg565,
                    uint16_t bg565) {
  int16_t ry = p.eye_ry;
  if (blinkAmt > 0.01f) {
    const float k = clamp01(blinkAmt);
    ry = (int16_t)(ry * (1.0f - k) + 2 * k);
    if (ry < 2) ry = 2;
  }
  const int16_t rx = p.eye_rx;

  if (p.eye_curve != 0 || ry < 5) {
    drawParabola(s, cx, cy, rx * 2, p.eye_curve, p.eye_stroke * 2, cosA, sinA, fg565);
    return;
  }

  s.fillEllipse(cx, cy, rx, ry, fg565);
  const int16_t irx = rx - p.eye_stroke;
  const int16_t iry = ry - p.eye_stroke;
  if (irx > 0 && iry > 0) {
    s.fillEllipse(cx, cy, irx, iry, bg565);
  }
  if (p.pupil_r > 0 && blinkAmt < 0.6f) {
    const float ldx = (float)(p.pupil_dx + gdx);
    const float ldy = (float)(p.pupil_dy + gdy);
    const float rdx = ldx * cosA - ldy * sinA;
    const float rdy = ldx * sinA + ldy * cosA;

    const float slackX = (float)irx - fabsf(rdx);
    const float slackY = (float)iry - fabsf(rdy);
    int16_t maxR = (int16_t)fminf(slackX, slackY);
    if (maxR < 1) return;
    int16_t effR = p.pupil_r;
    if (effR > maxR) effR = maxR;

    s.fillSmoothCircle(cx + (int16_t)rdx, cy + (int16_t)rdy, effR, fg565, bg565);
  }
}

static void drawHalfEllipse(TFT_eSprite& s, int16_t cx, int16_t cy, int16_t rx, int16_t ry,
                            float cosA, float sinA, uint16_t fg565) {
  if (rx < 1 || ry < 1) return;
  for (int16_t lx = -rx; lx <= rx; ++lx) {
    const float norm = (float)lx / (float)rx;
    const float h = (float)ry * sqrtf(fmaxf(0.0f, 1.0f - norm * norm));
    if (h < 0.5f) continue;
    const float top_rx = (float)lx * cosA;
    const float top_ry = (float)lx * sinA;
    const float bot_rx = (float)lx * cosA - h * sinA;
    const float bot_ry = (float)lx * sinA + h * cosA;
    s.drawLine(cx + (int16_t)top_rx, cy + (int16_t)top_ry, cx + (int16_t)bot_rx, cy + (int16_t)bot_ry,
               fg565);
  }
}

static void drawZigZagMouth(TFT_eSprite& s, int16_t cx, int16_t cy, int16_t width, int16_t amp,
                            int16_t thick, float cosA, float sinA, uint16_t fg565) {
  if (width < 8) return;
  if (thick < 1) thick = 1;
  const int16_t half = width / 2;
  const int16_t segments = 6;
  const float step = (float)width / (float)segments;
  float lx0 = (float)-half;
  float ly0 = 0.0f;
  for (int16_t i = 1; i <= segments; ++i) {
    const float lx1 = -half + step * i;
    const float ly1 = (i % 2 == 0) ? (float)-amp : (float)amp;
    const float rx0 = lx0 * cosA - ly0 * sinA;
    const float ry0 = lx0 * sinA + ly0 * cosA;
    const float rx1 = lx1 * cosA - ly1 * sinA;
    const float ry1 = lx1 * sinA + ly1 * cosA;
    for (int16_t o = -(thick / 2); o <= (thick / 2); ++o) {
      s.drawLine(cx + (int16_t)rx0, cy + (int16_t)ry0 + o, cx + (int16_t)rx1, cy + (int16_t)ry1 + o,
                 fg565);
    }
    lx0 = lx1;
    ly0 = ly1;
  }
}

static void drawMouth(TFT_eSprite& s, const FaceParams& p, int16_t cx, int16_t cy, Expression expr,
                      float cosA, float sinA, uint16_t fg565, uint16_t bg565) {
  if (expr == Expression::VerbStraining) {
    drawZigZagMouth(s, cx, cy, p.mouth_w * 2, 4, p.mouth_thick, cosA, sinA, fg565);
    return;
  }
  if (p.mouth_open_h > 0) {
    if (p.mouth_curve < 0) {
      drawHalfEllipse(s, cx, cy, p.mouth_w / 2, p.mouth_open_h, cosA, sinA, fg565);
    } else {
      s.fillEllipse(cx, cy, p.mouth_w / 2, p.mouth_open_h, fg565);
    }
    return;
  }
  drawParabola(s, cx, cy, p.mouth_w, p.mouth_curve, p.mouth_thick, cosA, sinA, fg565);
}

void drawFace(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx, int16_t gdy,
              Expression expr, uint16_t fg565, uint16_t bg565) {
  const float angleRad = (float)p.face_rot * (float)PI / 180.0f;
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

  drawEye(s, p, lex, ley, blinkAmt, gdx, gdy, cosA, sinA, fg565, bg565);
  drawEye(s, p, rex, rey, blinkAmt, gdx, gdy, cosA, sinA, fg565, bg565);
  drawMouth(s, p, mx, my, expr, cosA, sinA, fg565, bg565);
}

}  // namespace Face
