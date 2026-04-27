#include "TextScene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "MoodRingRenderer.h"

namespace Face {

static constexpr int16_t kInset = 20;
static constexpr int16_t kInnerW = 240 - kInset * 2;
static constexpr int16_t kCircleTextPad = 5;
static constexpr int16_t kTextInset = 5;
static constexpr int16_t kCircleRadius = 116;
static constexpr int16_t kLineAdvance = 14;

static void trimTrailing(char* line) {
  size_t n = strlen(line);
  while (n > 0 && line[n - 1] == ' ') {
    line[n - 1] = '\0';
    --n;
  }
}

static bool lineBoundsForY(int16_t y, int16_t& xMin, int16_t& xMax) {
  const int16_t dy = (int16_t)abs(y - kCy);
  if (dy >= kCircleRadius) return false;
  const float dx = sqrtf((float)(kCircleRadius * kCircleRadius - dy * dy));
  xMin = (int16_t)(kCx - dx) + kCircleTextPad;
  xMax = (int16_t)(kCx + dx) - kCircleTextPad;
  if (xMin < kInset) xMin = kInset;
  if (xMax > 239 - kInset) xMax = 239 - kInset;
  return xMax - xMin >= 24;
}

static bool textBoundsForLineInBox(int16_t y, int16_t boxX, int16_t boxW,
                                   int16_t& xMin, int16_t& xMax) {
  int16_t circleMin = 0, circleMax = 0;
  if (!lineBoundsForY(y, circleMin, circleMax)) return false;
  const int16_t boxMin = boxX;
  const int16_t boxMax = boxX + boxW - 1;
  xMin = circleMin > boxMin ? circleMin : boxMin;
  xMax = circleMax < boxMax ? circleMax : boxMax;
  xMin += kTextInset;
  xMax -= kTextInset;
  return xMax - xMin >= 12;
}

static void drawCircleAwareBox(TFT_eSprite& s, int16_t x, int16_t y, int16_t w,
                               int16_t h, uint16_t color) {
  const int16_t xBoxMin = x;
  const int16_t xBoxMax = x + w - 1;
  const int16_t yMin = y;
  const int16_t yMax = y + h - 1;

  for (int16_t yy = yMin; yy <= yMax; ++yy) {
    int16_t xMinCircle = 0, xMaxCircle = 0;
    if (!lineBoundsForY(yy, xMinCircle, xMaxCircle)) continue;

    const int16_t xClipMin = xBoxMin > xMinCircle ? xBoxMin : xMinCircle;
    const int16_t xClipMax = xBoxMax < xMaxCircle ? xBoxMax : xMaxCircle;
    if (xClipMin > xClipMax) continue;

    const bool topOrBottom = (yy == yMin || yy == yMax);
    if (topOrBottom) {
      s.drawFastHLine(xClipMin, yy, xClipMax - xClipMin + 1, color);
      continue;
    }

    if (xBoxMin >= xClipMin && xBoxMin <= xClipMax) {
      s.drawPixel(xBoxMin, yy, color);
    }
    if (xBoxMax >= xClipMin && xBoxMax <= xClipMax && xBoxMax != xBoxMin) {
      s.drawPixel(xBoxMax, yy, color);
    }
  }
}

static void drawCircleAwareBoxTopBottom(TFT_eSprite& s, int16_t x, int16_t y, int16_t w,
  int16_t h, uint16_t color) {
  const int16_t xBoxMin = x;
  const int16_t xBoxMax = x + w - 1;
  const int16_t yMin = y;
  const int16_t yMax = y + h - 1;

  int16_t yy = yMin;
  int16_t xMinCircle = 0, xMaxCircle = 0;
  if (!lineBoundsForY(yy, xMinCircle, xMaxCircle)) return;
  int16_t xClipMin = xBoxMin > xMinCircle ? xBoxMin : xMinCircle;
  int16_t xClipMax = xBoxMax < xMaxCircle ? xBoxMax : xMaxCircle;
  s.drawFastHLine(xClipMin, yy, xClipMax - xClipMin + 1, color);

  yy = yMax;
  if (!lineBoundsForY(yy, xMinCircle, xMaxCircle)) return;
  xClipMin = xBoxMin > xMinCircle ? xBoxMin : xMinCircle;
  xClipMax = xBoxMax < xMaxCircle ? xBoxMax : xMaxCircle;
  s.drawFastHLine(xClipMin, yy, xClipMax - xClipMin + 1, color);
}

static void drawWrappedInBox(TFT_eSprite& s, const char* text, int16_t boxX, int16_t boxW,
                             int16_t y, uint8_t maxLines) {
  if (!text || !*text || maxLines == 0) return;
  const char* p = text;
  for (uint8_t line = 0; line < maxLines && *p; ++line) {
    const int16_t yLine = y + line * kLineAdvance;
    int16_t xMin = 0, xMax = 0;
    if (!textBoundsForLineInBox(yLine, boxX, boxW, xMin, xMax)) continue;
    const int16_t maxWidthPx = xMax - xMin;

    char out[96];
    int16_t outLen = 0;
    int16_t lastSpace = -1;

    // Explicit newline forces a visual line break (important for markdown lists).
    if (*p == '\n' || *p == '\r') {
      if (*p == '\r' && *(p + 1) == '\n') ++p;
      ++p;
      continue;
    }

    while (*p == ' ') ++p;
    const char* start = p;
    while (*p && *p != '\n' && *p != '\r' && outLen < (int16_t)sizeof(out) - 1) {
      out[outLen] = *p;
      out[outLen + 1] = '\0';
      const int16_t trialWidth = s.textWidth(out);
      if (trialWidth > maxWidthPx) {
        break;
      }
      if (*p == ' ') lastSpace = outLen;
      ++outLen;
      ++p;
    }
    if (*p && *p != '\n' && *p != '\r' && outLen > 0 && lastSpace > 0 && s.textWidth(out) > maxWidthPx) {
      p = start + lastSpace + 1;
      outLen = lastSpace;
    }
    if (outLen == 0 && *p && *p != '\n' && *p != '\r') {
      out[0] = *p;
      out[1] = '\0';
      ++outLen;
      ++p;
    }
    if (*p == '\r') {
      ++p;
      if (*p == '\n') ++p;
    } else if (*p == '\n') {
      ++p;
    }
    out[outLen] = '\0';
    trimTrailing(out);
    if (out[0]) {
      s.drawString(out, xMin, yLine);
    }
  }
}

static void drawCenteredSingleLineInBox(TFT_eSprite& s, const char* text, int16_t boxX,
                                        int16_t boxW, int16_t y) {
  if (!text || !*text) return;
  int16_t xMin = 0, xMax = 0;
  if (!textBoundsForLineInBox(y, boxX, boxW, xMin, xMax)) return;
  const int16_t xCenter = xMin + ((xMax - xMin) / 2);
  s.setTextDatum(MC_DATUM);
  s.drawString(text, xCenter, y);
  s.setTextDatum(TL_DATUM);
}

void renderTextScene(TFT_eSprite& s, const SceneRenderState& renderState,
                     const AgentEvents::AgentState& agentState, uint32_t now) {
  (void)now;
  s.fillSprite(kBg);
  s.setTextFont(2);  // visually larger than default (about +50%-ish readability jump)
  s.setTextSize(1);
  s.setTextDatum(TL_DATUM);
  s.setTextColor(TFT_WHITE, TFT_BLACK);

  const int16_t statusY = 22;
  const int16_t statusH = 28;
  drawCircleAwareBoxTopBottom(s, kInset, statusY, kInnerW, statusH, TFT_DARKGREY);
  const char* status = agentState.status_line[0] ? agentState.status_line : "Idle";
  drawCenteredSingleLineInBox(s, status, kInset, kInnerW, statusY + (statusH / 2));

  const int16_t bodyY = statusY + statusH + 10;
  const int16_t bodyH = 160;
  drawCircleAwareBoxTopBottom(s, kInset, bodyY, kInnerW, bodyH, TFT_DARKGREY);
  const char* body = agentState.body_text[0] ? agentState.body_text : "(no message yet)";
  drawWrappedInBox(s, body, kInset, kInnerW, bodyY + 8, 10);

  if (moodRingEnabledFor(renderState.state)) {
    drawMoodRing(s, (uint8_t)renderState.mood_r, (uint8_t)renderState.mood_g,
                 (uint8_t)renderState.mood_b);
  }
}

}  // namespace Face
