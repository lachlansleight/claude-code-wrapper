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

static const char* driverName(uint8_t id) {
  switch (id) {
    case 1:
      return "pending_permission";
    case 2:
      return "straining";
    default:
      return "custom";
  }
}

static int16_t drawDebugWrappedLine(TFT_eSprite& s, int16_t y, uint16_t color, const char* text) {
  static constexpr int16_t kScreenW = 240;
  static constexpr int16_t kMaxTextW = (int16_t)(kScreenW * 0.8f);
  static constexpr int16_t kTextX = (int16_t)((kScreenW - kMaxTextW) / 2);
  static constexpr int16_t kLineAdvance = 10;

  if (!text || !*text) return (int16_t)(y + kLineAdvance);

  s.setTextColor(color, TFT_BLACK);
  const char* p = text;
  while (*p && y <= 232) {
    while (*p == ' ') ++p;
    if (!*p) break;

    char out[128];
    int16_t outLen = 0;
    int16_t lastSpace = -1;
    const char* start = p;
    while (*p && outLen < (int16_t)sizeof(out) - 1) {
      if (*p == '\n' || *p == '\r') break;
      out[outLen] = *p;
      out[outLen + 1] = '\0';
      if (s.textWidth(out) > kMaxTextW) break;
      if (*p == ' ') lastSpace = outLen;
      ++outLen;
      ++p;
    }

    if (outLen == 0) {
      out[0] = *p ? *p : '?';
      out[1] = '\0';
      if (*p) ++p;
    } else if (*p && *p != '\n' && *p != '\r' && lastSpace > 0 && s.textWidth(out) > kMaxTextW) {
      p = start + lastSpace + 1;
      outLen = lastSpace;
      out[outLen] = '\0';
    } else {
      out[outLen] = '\0';
      if (*p == '\r') {
        ++p;
        if (*p == '\n') ++p;
      } else if (*p == '\n') {
        ++p;
      }
    }

    trimTrailing(out);
    if (out[0]) {
      s.drawString(out, kTextX, y);
      y = (int16_t)(y + kLineAdvance);
    }
  }

  return y;
}

static void renderDebugScene(TFT_eSprite& s, const SceneRenderState& renderState, const SceneContext& ctx,
                             uint32_t now) {
  (void)renderState;
  s.fillSprite(TFT_BLACK);
  s.setTextSize(1);
  s.setTextFont(1);
  s.setTextDatum(TL_DATUM);

  const uint16_t fg = rgb888To565(ctx.fg_r, ctx.fg_g, ctx.fg_b);
  const uint16_t accent = rgb888To565(ctx.accent_r, ctx.accent_g, ctx.accent_b);
  const uint16_t muted = rgb888To565(160, 160, 160);
  const uint16_t warn = rgb888To565(255, 180, 80);

  static constexpr int16_t kDebugLineAdvance = 10;
  int16_t y = (int16_t)(6 + 4 * kDebugLineAdvance);
  char line[128];

  y = drawDebugWrappedLine(s, y, accent, "DEBUG MODE");
  snprintf(line, sizeof(line), "expr=%s", expressionName(ctx.effective_expression));
  y = drawDebugWrappedLine(s, y, fg, line);
  snprintf(line, sizeof(line), "mood v=%.2f a=%.2f", (double)ctx.mood_v, (double)ctx.mood_a);
  y = drawDebugWrappedLine(s, y, fg, line);

  if (ctx.pending_snap_active) {
    const uint32_t pendingMs = (ctx.pending_snap_since_ms > 0 && now > ctx.pending_snap_since_ms)
                                   ? (now - ctx.pending_snap_since_ms)
                                   : 0;
    snprintf(line, sizeof(line), "snap=%s pending=%s(%lums)", ctx.snapped_emotion,
             ctx.pending_snapped_emotion, (unsigned long)pendingMs);
  } else {
    snprintf(line, sizeof(line), "snap=%s", ctx.snapped_emotion);
  }
  y = drawDebugWrappedLine(s, y, fg, line);

  snprintf(line, sizeof(line), "verb cur=%s eff=%s", ctx.verb_current, ctx.verb_effective);
  y = drawDebugWrappedLine(s, y, fg, line);
  snprintf(line, sizeof(line), "verb t=%lums linger=%lums", (unsigned long)ctx.verb_time_in_current_ms,
           (unsigned long)ctx.verb_linger_remaining_ms);
  y = drawDebugWrappedLine(s, y, fg, line);
  snprintf(line, sizeof(line), "overlay=%s queued=%s rem=%lums", ctx.verb_overlay_active ? "on" : "off",
           ctx.verb_overlay_queued ? "yes" : "no", (unsigned long)ctx.verb_overlay_remaining_ms);
  y = drawDebugWrappedLine(s, y, fg, line);

  y = drawDebugWrappedLine(s, y, muted, "held valence drivers:");
  if (ctx.held_driver_count == 0) {
    y = drawDebugWrappedLine(s, y, muted, "(none)");
  } else {
    for (uint8_t i = 0; i < ctx.held_driver_count && i < 8; ++i) {
      snprintf(line, sizeof(line), "  %u:%s=%.2f", ctx.held_driver_ids[i], driverName(ctx.held_driver_ids[i]),
               (double)ctx.held_driver_targets[i]);
      y = drawDebugWrappedLine(s, y, fg, line);
      if (y > 210) break;
    }
  }

  if (ctx.pending_permission[0]) {
    snprintf(line, sizeof(line), "pending permission=%s", ctx.pending_permission);
    y = drawDebugWrappedLine(s, y, warn, line);
  }
}

void renderTextScene(TFT_eSprite& s, const SceneRenderState& renderState, const SceneContext& ctx,
                     uint32_t now) {
  if (ctx.render_mode == (uint8_t)RenderMode::Debug) {
    renderDebugScene(s, renderState, ctx, now);
    return;
  }

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
