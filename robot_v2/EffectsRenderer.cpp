#include "EffectsRenderer.h"

#include <math.h>

#include "SceneTypes.h"

namespace Face {

static uint32_t mixBits(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static uint8_t alphaScale8(uint8_t c, float a) {
  if (a <= 0.0f) return 0;
  if (a >= 1.0f) return c;
  return (uint8_t)((float)c * a);
}

static void tokenRgb(uint32_t tok, uint8_t& r, uint8_t& g, uint8_t& b) {
  const uint32_t cPick = tok % 6U;
  r = 120;
  g = 160;
  b = 230;
  if (cPick == 0) {
    r = 120;
    g = 220;
    b = 255;
  } else if (cPick == 1) {
    r = 180;
    g = 135;
    b = 255;
  } else if (cPick == 2) {
    r = 255;
    g = 190;
    b = 90;
  } else if (cPick == 3) {
    r = 130;
    g = 235;
    b = 160;
  } else if (cPick == 4) {
    r = 255;
    g = 120;
    b = 170;
  }
}

static constexpr int16_t kScreenW = 240;

static void drawReadStreamEffect(TFT_eSprite& s, uint32_t now, float alpha) {
  if (alpha <= 0.01f) return;

  const float vis = 0.5f * clamp01(alpha);
  static constexpr int16_t kTop = 14;
  static constexpr int16_t kBottom = 226;
  const int16_t xBandMin = 0;
  const int16_t xBandMax = (int16_t)(kScreenW / 2);
  static constexpr int16_t lineHeight = 2;
  static constexpr float animateSpeed = 200.0f;
  static constexpr int32_t kVirtualLinesPerParagraph = 14;

  const float scrollPx = (float)now * (animateSpeed / 1000.0f);

  for (int16_t y = kTop; y + lineHeight <= kBottom; y = (int16_t)(y + lineHeight)) {
    const int16_t row = (int16_t)((y - kTop) / lineHeight);
    const int32_t lineIdx =
        (int32_t)floorf((float)row + scrollPx / (float)lineHeight + 1.0e6f);
    const int32_t p0 =
        (lineIdx / kVirtualLinesPerParagraph) * kVirtualLinesPerParagraph;
    const int32_t rel = lineIdx - p0;
    const uint32_t ph = mixBits((uint32_t)p0 ^ 0xdeadbeefU);
    const uint32_t logicalSpan = 5u + (ph % 46u);
    if ((ph + (uint32_t)rel * 17U) % logicalSpan == 0U) continue;

    const int16_t indentSteps =
        (int16_t)((ph % 7U) +
                  (rel > kVirtualLinesPerParagraph / 2
                       ? (int16_t)((ph >> 3) % 4U)
                       : 0));
    const int16_t indentPx = (int16_t)(4 + indentSteps * 4);

    int16_t x = (int16_t)(xBandMin + indentPx);
    const int16_t xMax = xBandMax;
    const int16_t avail = (int16_t)(xMax - x);
    if (avail < 10) continue;

    const uint32_t wLine = mixBits((uint32_t)lineIdx ^ ph ^ 0x51edc3baU);
    int16_t lineEndX;
    if ((wLine % 8U) == 0U) {
      const int32_t lo = (int32_t)x + ((int32_t)avail * 62) / 100;
      const int32_t hi = (int32_t)xMax;
      const int32_t span = hi - lo;
      lineEndX = (span < 8)
                     ? xMax
                     : (int16_t)(lo + 8 +
                                 (int32_t)((wLine >> 5) % (uint32_t)(span - 7)));
    } else {
      const int32_t pct = (int32_t)(16 + (wLine % 34));
      lineEndX = (int16_t)(x + ((int32_t)avail * pct) / 100);
      const int16_t minEnd = (int16_t)(x + 12);
      const int16_t cap = (int16_t)(x + ((int32_t)avail * 52) / 100);
      if (lineEndX < minEnd) lineEndX = minEnd;
      if (lineEndX > cap) lineEndX = cap;
    }
    if (lineEndX > xMax) lineEndX = xMax;

    uint32_t tok = mixBits((uint32_t)lineIdx ^ (uint32_t)p0 * 0x27d4eb2dU);
    while (x < lineEndX) {
      const int16_t runW = (int16_t)(2 + (tok % 3) * 2);
      uint8_t r, g, b;
      tokenRgb(tok, r, g, b);
      const uint16_t tokenColor =
          rgb888To565(alphaScale8(r, vis), alphaScale8(g, vis), alphaScale8(b, vis));
      if (x + runW > xBandMin && x < xBandMax) {
        const int16_t x0 = x < xBandMin ? xBandMin : x;
        const int16_t x1 = (int16_t)(x + runW);
        const int16_t clipR = x1 > xBandMax ? xBandMax : x1;
        const int16_t wClip = (int16_t)(clipR - x0);
        if (wClip > 0) s.fillRect(x0, y, wClip, lineHeight, tokenColor);
      }
      x = (int16_t)(x + runW);
      const int16_t gap = (int16_t)(1 + (tok >> 8) % 3U);
      x = (int16_t)(x + gap);
      tok = mixBits(tok + 0x6d2b79f5U + (uint32_t)x);
    }
  }
}

static void drawWriteStreamEffect(TFT_eSprite& s, uint32_t now, float alpha) {
  if (alpha <= 0.01f) return;

  const float vis = 0.5f * clamp01(alpha);
  static constexpr int16_t kTop = 14;
  const int16_t xBandMin = (int16_t)(kScreenW / 2);
  const int16_t xBandMax = kScreenW;
  static constexpr int16_t lineHeight = 4;
  static constexpr float animateSpeed = 100.0f;
  static constexpr int32_t kVirtualLinesPerParagraph = 18;

  const float scrollPx = (float)now * (animateSpeed / 1000.0f);

  for (int16_t y = kTop; y + lineHeight <= kCy; y = (int16_t)(y + lineHeight)) {
    const int16_t row = (int16_t)((y - kTop) / lineHeight);
    const int32_t lineIdx =
        (int32_t)floorf((float)row + scrollPx / (float)lineHeight + 1.0e6f);
    const int32_t p0 =
        (lineIdx / kVirtualLinesPerParagraph) * kVirtualLinesPerParagraph;
    const int32_t rel = lineIdx - p0;
    const uint32_t ph = mixBits((uint32_t)p0 ^ 0x5a5a5a5aU);
    const uint32_t logicalSpan = 6u + (ph % 40u);
    if ((ph + (uint32_t)rel * 17U) % logicalSpan == 0U) continue;

    const int16_t indentSteps =
        (int16_t)((ph % 7U) +
                  (rel > kVirtualLinesPerParagraph / 2
                       ? (int16_t)((ph >> 3) % 4U)
                       : 0));
    const int16_t indentPx = (int16_t)(4 + indentSteps * 4);

    int16_t x = (int16_t)(xBandMin + indentPx);
    const int16_t xMax = xBandMax;
    const int16_t avail = (int16_t)(xMax - x);
    if (avail < 10) continue;

    const uint32_t wLine = mixBits((uint32_t)lineIdx ^ ph ^ 0x51edc3baU);
    int16_t lineEndX;
    if ((wLine % 8U) == 0U) {
      const int32_t lo = (int32_t)x + ((int32_t)avail * 62) / 100;
      const int32_t hi = (int32_t)xMax;
      const int32_t span = hi - lo;
      lineEndX = (span < 8)
                     ? xMax
                     : (int16_t)(lo + 8 +
                                 (int32_t)((wLine >> 5) % (uint32_t)(span - 7)));
    } else {
      const int32_t pct = (int32_t)(16 + (wLine % 34));
      lineEndX = (int16_t)(x + ((int32_t)avail * pct) / 100);
      const int16_t minEnd = (int16_t)(x + 12);
      const int16_t cap = (int16_t)(x + ((int32_t)avail * 52) / 100);
      if (lineEndX < minEnd) lineEndX = minEnd;
      if (lineEndX > cap) lineEndX = cap;
    }
    if (lineEndX > xMax) lineEndX = xMax;

    uint32_t tok = mixBits((uint32_t)lineIdx ^ (uint32_t)p0 * 0x27d4eb2dU);
    while (x < lineEndX) {
      const int16_t runW = (int16_t)(2 + (tok % 3) * 2);
      uint8_t r, g, b;
      tokenRgb(tok, r, g, b);
      const uint16_t tokenColor =
          rgb888To565(alphaScale8(r, vis), alphaScale8(g, vis), alphaScale8(b, vis));
      if (x + runW > xBandMin && x < xBandMax) {
        const int16_t x0 = x < xBandMin ? xBandMin : x;
        const int16_t x1 = (int16_t)(x + runW);
        const int16_t clipR = x1 > xBandMax ? xBandMax : x1;
        const int16_t wClip = (int16_t)(clipR - x0);
        if (wClip > 0) s.fillRect(x0, y, wClip, lineHeight, tokenColor);
      }
      x = (int16_t)(x + runW);
      const int16_t gap = (int16_t)(1 + (tok >> 8) % 3U);
      x = (int16_t)(x + gap);
      tok = mixBits(tok + 0x6d2b79f5U + (uint32_t)x);
    }
  }
}

void drawEffects(TFT_eSprite& s, uint32_t now, float readAlpha, float writeAlpha) {
  drawReadStreamEffect(s, now, readAlpha);
  drawWriteStreamEffect(s, now, writeAlpha);
}

}  // namespace Face
