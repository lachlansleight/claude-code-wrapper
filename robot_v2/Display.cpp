#include "Display.h"

#include <TFT_eSPI.h>
#include <math.h>
#include <string.h>

#include "ClaudeEvents.h"
#include "DebugLog.h"
#include "ToolFormat.h"
#include "config.h"

namespace Display {

// 240x240 round panel. We keep all drawing inside a 100px radius (20px
// chord padding from the rim) so text can't run off the circle.
static constexpr int16_t kW       = 240;
static constexpr int16_t kH       = 240;
static constexpr int16_t kCx      = 120;
static constexpr int16_t kCy      = 120;
static constexpr int16_t kDrawR   = 100;

// Header strip: three small icons centered along the top of the safe area.
static constexpr int16_t kHeaderTop = 24;
static constexpr int16_t kIconSize  = 16;
static constexpr int16_t kIconGap   = 12;

// Body region. kBodyTop sits just below the header divider; kBodyBot is the
// last allowed text bottom (below this we'd be outside the safe circle on
// the lower edge anyway).
static constexpr int16_t kBodyTop    = 60;
static constexpr int16_t kBodyBot    = 220;
static constexpr int16_t kLineHeight = 20;   // size-2 default font is 16px tall

static constexpr uint32_t kMinRedrawMs     = 33;   // ~30 fps cap for now
static constexpr uint32_t kSpinnerPeriodMs = 120;
static constexpr uint32_t kBlinkPeriodMs   = 500;
static constexpr uint32_t kToolLingerMs    = 1000;

static constexpr uint16_t kColBg     = TFT_BLACK;
static constexpr uint16_t kColFg     = TFT_WHITE;
static constexpr uint16_t kColDim    = 0x4208;       // dark grey
static constexpr uint16_t kColAccent = TFT_CYAN;
static constexpr uint16_t kColWarn   = TFT_YELLOW;

static TFT_eSPI    tft;
static TFT_eSprite fb(&tft);
static bool        fbReady = false;

static bool     dirty = true;
static uint32_t lastRedraw = 0;
static uint8_t  spinnerFrame = 0;

// ---- Geometry --------------------------------------------------------------

// Half-chord of the safe circle at row y. Returns 0 when y is outside.
static int16_t halfChordAt(int16_t y) {
  const int16_t dy = y - kCy;
  const int32_t r2 = (int32_t)kDrawR * kDrawR - (int32_t)dy * dy;
  if (r2 <= 0) return 0;
  return (int16_t)sqrtf((float)r2);
}

// ---- Icons (drawn into the sprite, all 16x16) -----------------------------

static void drawWifi(int16_t x, int16_t y, uint16_t col) {
  fb.fillRect(x + 1,  y + 12, 3, 4,  col);
  fb.fillRect(x + 6,  y + 8,  3, 8,  col);
  fb.fillRect(x + 11, y + 4,  3, 12, col);
}

static void drawCheck(int16_t x, int16_t y, uint16_t col) {
  fb.drawLine(x + 2,  y + 9,  x + 6,  y + 13, col);
  fb.drawLine(x + 6,  y + 13, x + 14, y + 4,  col);
  fb.drawLine(x + 2,  y + 10, x + 6,  y + 14, col);
  fb.drawLine(x + 6,  y + 14, x + 14, y + 5,  col);
}

static void drawCross(int16_t x, int16_t y, uint16_t col) {
  fb.drawLine(x + 3,  y + 3,  x + 12, y + 12, col);
  fb.drawLine(x + 12, y + 3,  x + 3,  y + 12, col);
  fb.drawLine(x + 3,  y + 4,  x + 11, y + 12, col);
  fb.drawLine(x + 12, y + 4,  x + 4,  y + 12, col);
}

// 4-frame line spinner (horizontal → diag → vertical → diag).
static void drawSpinner(int16_t x, int16_t y, uint8_t frame, uint16_t col) {
  const int16_t cx = x + 8, cy = y + 8;
  const int16_t r = 6;
  switch (frame & 3) {
    case 0:
      fb.drawLine(cx - r, cy,     cx + r, cy,     col);
      fb.drawLine(cx - r, cy + 1, cx + r, cy + 1, col);
      break;
    case 1:
      fb.drawLine(cx - r, cy + r,     cx + r, cy - r,     col);
      fb.drawLine(cx - r, cy + r - 1, cx + r, cy - r - 1, col);
      break;
    case 2:
      fb.drawLine(cx,     cy - r, cx,     cy + r, col);
      fb.drawLine(cx + 1, cy - r, cx + 1, cy + r, col);
      break;
    case 3:
      fb.drawLine(cx - r, cy - r,     cx + r, cy + r,     col);
      fb.drawLine(cx - r, cy - r + 1, cx + r, cy + r + 1, col);
      break;
  }
}

// 8x8 dot, filled when on / outlined when off — the idle blink.
static void drawIdleDot(int16_t x, int16_t y, bool on, uint16_t col) {
  if (on) fb.fillRect(x + 4, y + 4, 8, 8, col);
  else    fb.drawRect(x + 4, y + 4, 8, 8, kColDim);
}

// ---- Header ---------------------------------------------------------------

static void drawHeader() {
  const auto& st = ClaudeEvents::state();

  const int16_t totalW = kIconSize * 3 + kIconGap * 2;
  int16_t x = kCx - totalW / 2;
  const int16_t y = kHeaderTop;

  drawWifi(x, y, st.wifi_connected ? kColFg : kColDim);
  x += kIconSize + kIconGap;

  if (st.ws_connected) drawCheck(x, y, kColAccent);
  else                 drawCross(x, y, kColWarn);
  x += kIconSize + kIconGap;

  if (st.working) {
    drawSpinner(x, y, spinnerFrame, kColFg);
  } else {
    const bool on = (millis() / kBlinkPeriodMs) % 2 == 0;
    drawIdleDot(x, y, on, kColFg);
  }

  // Subtle separator under the header — chord, not a full hline, so it
  // stays inside the circle.
  const int16_t sepY = y + kIconSize + 6;
  const int16_t half = halfChordAt(sepY);
  if (half > 0) {
    fb.drawFastHLine(kCx - half, sepY, half * 2, kColDim);
  }
}

// ---- Body text ------------------------------------------------------------

// Greedy word-wrap. For each row, the available pixel width is the chord
// of the safe circle at that row's vertical centre — so lines at the top
// and bottom of the body region naturally get narrower. Text is centred
// horizontally on each row. Hard-breaks if a single word is wider than
// the chord.
static void drawBodyText(const char* text) {
  if (!text || !*text) return;

  fb.setTextColor(kColFg, kColBg);
  fb.setTextSize(2);

  size_t pos = 0;
  const size_t len = strlen(text);
  int16_t y = kBodyTop;

  while (pos < len) {
    if (y + kLineHeight > kBodyBot) break;

    while (pos < len && text[pos] == ' ') ++pos;
    if (pos >= len) break;

    const int16_t mid = y + kLineHeight / 2;
    const int16_t maxWidth = halfChordAt(mid) * 2;
    if (maxWidth <= 0) { y += kLineHeight; continue; }

    char buf[64];
    size_t take = 0;
    size_t lastSpace = 0;     // index within `take` of the last space
    bool sawSpace = false;
    while (pos + take < len && take < sizeof(buf) - 1) {
      buf[take]     = text[pos + take];
      buf[take + 1] = '\0';
      if (fb.textWidth(buf) > maxWidth) break;
      if (text[pos + take] == ' ') { lastSpace = take; sawSpace = true; }
      ++take;
    }

    size_t cut;
    if (pos + take >= len) {
      cut = take;                              // rest of string fits
    } else if (sawSpace) {
      cut = lastSpace;                         // break at last space
    } else {
      cut = take > 0 ? take : 1;               // word too long: hard break
    }

    char line[64];
    memcpy(line, text + pos, cut);
    line[cut] = '\0';
    while (cut > 0 && line[cut - 1] == ' ') line[--cut] = '\0';

    const int16_t w = fb.textWidth(line);
    fb.setCursor(kCx - w / 2, y);
    fb.print(line);

    pos += (cut > 0 ? cut : 1);
    y += kLineHeight;
  }
}

static bool toolSlotActive(const ClaudeEvents::ClaudeState& st) {
  if (!st.current_tool[0]) return false;
  if (st.current_tool_end_ms == 0) return true;
  return (millis() - st.current_tool_end_ms) < kToolLingerMs;
}

static void drawBody() {
  const auto& st = ClaudeEvents::state();

  char buf[160];
  if (st.pending_permission[0]) {
    const char* label = ToolFormat::label(st.pending_tool);
    if (st.pending_detail[0]) {
      snprintf(buf, sizeof(buf), "ALLOW? %s %s", label, st.pending_detail);
    } else {
      snprintf(buf, sizeof(buf), "ALLOW? %s", label);
    }
    drawBodyText(buf);
  } else if (toolSlotActive(st)) {
    const char* label = ToolFormat::label(st.current_tool);
    if (st.tool_detail[0]) {
      snprintf(buf, sizeof(buf), "%s %s", label, st.tool_detail);
    } else {
      snprintf(buf, sizeof(buf), "%s", label);
    }
    drawBodyText(buf);
  } else if (st.last_summary[0]) {
    drawBodyText(st.last_summary);
  } else if (!st.ws_connected) {
    drawBodyText("waiting for bridge");
  } else if (st.working) {
    drawBodyText("Thinking...");
  }
}

// ---- Redraw bookkeeping ---------------------------------------------------

static uint32_t lastEventSnapshot = 0;
static bool     lastWs = false;
static bool     lastWifi = false;
static bool     lastWorking = false;
static uint32_t lastBlinkBucket = 0;

static bool stateChanged() {
  const auto& st = ClaudeEvents::state();
  if (st.last_event_ms != lastEventSnapshot) return true;
  if (st.ws_connected   != lastWs)           return true;
  if (st.wifi_connected != lastWifi)         return true;
  if (st.working        != lastWorking)      return true;
  return false;
}

static void snapshot() {
  const auto& st = ClaudeEvents::state();
  lastEventSnapshot = st.last_event_ms;
  lastWs            = st.ws_connected;
  lastWifi          = st.wifi_connected;
  lastWorking       = st.working;
}

// ---- Backlight ------------------------------------------------------------

static void backlightInit() {
#ifdef TFT_BL
  // Arduino-ESP32 3.x LEDC API: pin-addressed, no channel bookkeeping.
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, 255);
#endif
}

void setBrightness(uint8_t pct) {
#ifdef TFT_BL
  if (pct > 100) pct = 100;
  ledcWrite(TFT_BL, (uint32_t)pct * 255 / 100);
#else
  (void)pct;
#endif
}

// ---- Push -----------------------------------------------------------------

static void pushFrame() {
  tft.startWrite();
  tft.dmaWait();
  tft.pushImageDMA(0, 0, kW, kH, (uint16_t*)fb.getPointer());
  tft.endWrite();
}

// ---- Public API -----------------------------------------------------------

void begin() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.initDMA();

  fb.setColorDepth(16);
  fb.createSprite(kW, kH);
  fbReady = fb.created();
  if (!fbReady) {
    LOG_ERR("framebuffer alloc failed (%d bytes)", kW * kH * 2);
    backlightInit();
    return;
  }
  fb.fillSprite(kColBg);

  backlightInit();

  // Boot splash — also exercises the DMA path.
  fb.fillSprite(kColBg);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(kColFg, kColBg);
  fb.setTextSize(2);
  fb.drawString("robot_v2", kCx, kCy - 12);
  fb.setTextSize(1);
  fb.drawString("booting...", kCx, kCy + 14);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

void invalidate() { dirty = true; }

void drawPortalScreen(const char* ssid, const char* ip) {
  if (!fbReady) return;
  fb.fillSprite(kColBg);
  fb.setTextDatum(MC_DATUM);
  fb.setTextSize(2);
  fb.setTextColor(kColWarn, kColBg);
  fb.drawString("CONFIG MODE", kCx, kCy - 36);
  fb.setTextColor(kColFg, kColBg);
  fb.drawString(ssid, kCx, kCy);
  fb.setTextSize(1);
  fb.drawString(ip, kCx, kCy + 28);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

void tick() {
  if (!fbReady) return;
  const uint32_t now = millis();
  if (now - lastRedraw < kMinRedrawMs) return;

  const auto& st = ClaudeEvents::state();

  bool animTick = false;
  if (st.working) {
    if (now - lastRedraw >= kSpinnerPeriodMs) {
      animTick = true;
      spinnerFrame++;
    }
  } else {
    const uint32_t bucket = now / kBlinkPeriodMs;
    if (bucket != lastBlinkBucket) {
      animTick = true;
      lastBlinkBucket = bucket;
    }
  }

  if (!dirty && !stateChanged() && !animTick) return;

  lastRedraw = now;
  dirty = false;
  snapshot();

  fb.fillSprite(kColBg);
  drawHeader();
  drawBody();
  pushFrame();
}

}  // namespace Display
