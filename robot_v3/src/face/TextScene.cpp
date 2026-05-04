#include "TextScene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/AsciiCopy.h"
#include "MoodRingRenderer.h"

namespace Face {

static constexpr int16_t kInset = 20;
static constexpr int16_t kInnerW = 240 - kInset * 2;
static constexpr int16_t kCircleTextPad = 5;
static constexpr int16_t kTextInset = 10;
static constexpr int16_t kCircleRadius = 116;
static constexpr int16_t kLineAdvanceBody = 14;
static constexpr int16_t kSubtitleGapPx = 2;
static constexpr int16_t kHeaderPadY = 6;
static constexpr int16_t kBodyTopGap = 5;
static constexpr int16_t kBottomMargin = 15;

static void trimTrailing(char* line) {
  size_t n = strlen(line);
  while (n > 0 && line[n - 1] == ' ') {
    line[n - 1] = '\0';
    --n;
  }
}

static bool circleChordAtY(int16_t y, int16_t& xMin, int16_t& xMax) {
  const int16_t dy = (int16_t)abs(y - kCy);
  if (dy >= kCircleRadius) return false;
  const float dx = sqrtf((float)(kCircleRadius * kCircleRadius - dy * dy));
  xMin = (int16_t)(kCx - dx) + kCircleTextPad;
  xMax = (int16_t)(kCx + dx) - kCircleTextPad;
  if (xMin < 0) xMin = 0;
  if (xMax > 239) xMax = 239;
  return xMax - xMin >= 12;
}

static bool textBoundsForLineOnCircle(int16_t y, int16_t& xMin, int16_t& xMax) {
  if (!circleChordAtY(y, xMin, xMax)) return false;
  xMin += kTextInset;
  xMax -= kTextInset;
  return xMax - xMin >= 12;
}

static void drawLineWithinCircle(TFT_eSprite& s, int16_t y, int16_t inset, uint16_t color) {
  int16_t xMin = 0, xMax = 0;
  if (!textBoundsForLineOnCircle(y, xMin, xMax)) return;
  xMin += inset;
  xMax -= inset;
  s.drawFastHLine(xMin, y, xMax - xMin + 1, color);
}

static void drawWrappedOnCircle(TFT_eSprite& s, const char* text, int16_t y0, int16_t yMax,
                                uint8_t maxLines) {
  if (!text || !*text || maxLines == 0) return;
  const int16_t fontH = s.fontHeight();
  const char* p = text;
  for (uint8_t line = 0; line < maxLines && *p; ++line) {
    const int16_t yTop = (int16_t)(y0 + line * kLineAdvanceBody);
    if (yTop > yMax) break;
    const int16_t yChord = (int16_t)(yTop + fontH / 2);
    int16_t xMin = 0, xMax = 0;
    if (!textBoundsForLineOnCircle(yChord, xMin, xMax)) continue;
    const int16_t maxWidthPx = xMax - xMin;

    char out[96];
    int16_t outLen = 0;
    int16_t lastSpace = -1;

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
      s.drawString(out, xMin, yTop);
    }
  }
}

static void drawCenteredOnCircle(TFT_eSprite& s, const char* text, int16_t yCenter) {
  if (!text || !*text) return;
  int16_t xMin = 0, xMax = 0;
  if (!textBoundsForLineOnCircle(yCenter, xMin, xMax)) {
    s.setTextDatum(MC_DATUM);
    s.drawString(text, kCx, yCenter);
    s.setTextDatum(TL_DATUM);
    return;
  }
  const int16_t cx = (xMin + xMax) / 2;
  s.setTextDatum(MC_DATUM);
  s.drawString(text, cx, yCenter);
  s.setTextDatum(TL_DATUM);
}

static void buildSubtitleLine(char* out, size_t cap, const SceneContext& ctx, uint32_t now) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  const char* title = ctx.status_line[0] ? ctx.status_line : "";

  if (!strcmp(title, "Thinking")) {
    const uint32_t t0 = ctx.thinking_title_since_ms;
    const uint32_t elapsed = (t0 != 0u) ? (uint32_t)(now - t0) : 0u;
    const unsigned long sec = (unsigned long)(elapsed / 1000u);
    snprintf(out, cap, "%lus", sec);
    return;
  }

  if (!strcmp(title, "Done")) {
    const uint32_t elapsed = ctx.done_turn_elapsed_ms;
    if (ctx.turn_started_wall_ms == 0u && elapsed == 0u) {
      snprintf(out, cap, "—");
      return;
    }
    const unsigned long totalSec = (unsigned long)(elapsed / 1000u);
    const unsigned long mins = totalSec / 60u;
    const unsigned long secs = totalSec % 60u;
    if (mins > 0u) {
      snprintf(out, cap, "%lum %lus", mins, secs);
    } else {
      snprintf(out, cap, "%lus", secs);
    }
    return;
  }

  if (ctx.subtitle_tool[0]) {
    AsciiCopy::copy(out, cap, ctx.subtitle_tool);
  }
}

void renderTextScene(TFT_eSprite& s, const SceneRenderState& renderState, const SceneContext& ctx,
                     uint32_t now) {
  s.fillSprite(renderState.bg565);
  s.setTextSize(1);
  s.setTextDatum(TL_DATUM);

  const bool sleepUi = (renderState.expression == Expression::VerbSleeping);
  const char* titleStr =
      sleepUi ? "Zzz..." : (ctx.status_line[0] ? ctx.status_line : "Idle");

  uint16_t titleColor = renderState.fg565;
  if (moodRingEnabledFor(renderState.expression)) {
    int r = (int)lroundf(renderState.mood_r);
    int g = (int)lroundf(renderState.mood_g);
    int b = (int)lroundf(renderState.mood_b);
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    const int lum = r + g + b;
    if (lum >= 40) {
      titleColor = rgb888To565((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
  }

  const int16_t kHeaderAnchorY = 15;

  s.setTextFont(2);
  s.setTextSize(2);
  const int16_t titleFontH = s.fontHeight();
  s.setTextSize(1);
  const int16_t subFontH = s.fontHeight();

  const int16_t titleCenterY = kHeaderAnchorY + kHeaderPadY + titleFontH / 2;
  const int16_t subtitleCenterY =
      titleCenterY + (titleFontH + subFontH) / 2 + kSubtitleGapPx;

  int16_t headerBoxTop = (int16_t)(titleCenterY - titleFontH / 2 - kHeaderPadY);
  if (headerBoxTop < kHeaderAnchorY) headerBoxTop = kHeaderAnchorY;
  const int16_t headerH =
      (int16_t)((subtitleCenterY + subFontH / 2 + kHeaderPadY) - headerBoxTop);

  (void)kInnerW;
  (void)headerH;

  s.setTextSize(2);
  s.setTextColor(titleColor, renderState.bg565);
  drawCenteredOnCircle(s, titleStr, titleCenterY);

  char subBuf[sizeof(ctx.subtitle_tool) + 24];
  subBuf[0] = '\0';
  if (!sleepUi) {
    buildSubtitleLine(subBuf, sizeof(subBuf), ctx, now);
  }

  s.setTextSize(1);
  s.setTextColor(renderState.fg565, renderState.bg565);
  if (!sleepUi && subBuf[0]) {
    drawCenteredOnCircle(s, subBuf, subtitleCenterY);
  }

  const int16_t bodyY = (int16_t)(headerBoxTop + headerH + kBodyTopGap);
  const int16_t bodyMaxY = (int16_t)(240 - kBottomMargin);
  const int16_t bodyH = (int16_t)(bodyMaxY - bodyY);
  if (bodyH > kLineAdvanceBody * 2) {
    drawLineWithinCircle(s, bodyY - kBodyTopGap / 2, kInset, renderState.divider565);
    if (!sleepUi && ctx.body_text[0]) {
      s.setTextColor(renderState.fg565, renderState.bg565);
      const uint8_t maxLines =
          (uint8_t)((bodyH - 10) / kLineAdvanceBody > 24 ? 24 : (bodyH - 10) / kLineAdvanceBody);
      drawWrappedOnCircle(s, ctx.body_text, (int16_t)(bodyY + 6), bodyMaxY - 4, maxLines);
    }
  }

  if (moodRingEnabledFor(renderState.expression)) {
    drawMoodRing(s, (uint8_t)renderState.mood_r, (uint8_t)renderState.mood_g, (uint8_t)renderState.mood_b);
  }
}

}  // namespace Face
