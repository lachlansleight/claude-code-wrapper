#include "Display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <string.h>

#include "ClaudeEvents.h"
#include "DebugLog.h"
#include "config.h"

namespace Display {

// 6x8 glyphs at size 1 → 21 visible columns. Body is 2 rows.
static constexpr uint8_t kCols = 21;
static constexpr uint8_t kBodyY0 = 16;
static constexpr uint8_t kBodyY1 = 24;

// Redraw pacing. Working state needs a fast tick for the spinner; idle
// only needs to redraw when the blink flips.
static constexpr uint32_t kMinRedrawMs      = 40;
static constexpr uint32_t kSpinnerPeriodMs  = 120;
static constexpr uint32_t kBlinkPeriodMs    = 500;

static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

static bool     dirty = true;
static uint32_t lastRedraw = 0;
static uint8_t  spinnerFrame = 0;

// ---- Icons (all line-drawn) ------------------------------------------------

// Three signal bars of increasing height. Footprint: 7 wide × 7 tall.
// Reads unambiguously as "wifi strength" at small sizes; avoids the stray
// baseline pixels that the concentric-arcs version produced.
static void drawWifi(int x, int y) {
  oled.drawFastVLine(x + 0, y + 5, 2, SSD1306_WHITE);
  oled.drawFastVLine(x + 1, y + 5, 2, SSD1306_WHITE);
  oled.drawFastVLine(x + 3, y + 3, 4, SSD1306_WHITE);
  oled.drawFastVLine(x + 4, y + 3, 4, SSD1306_WHITE);
  oled.drawFastVLine(x + 6, y + 1, 6, SSD1306_WHITE);
  oled.drawFastVLine(x + 7, y + 1, 6, SSD1306_WHITE);
}

// Tick mark. Footprint: 7 wide × 7 tall.
static void drawCheck(int x, int y) {
  oled.drawLine(x + 0, y + 4, x + 2, y + 6, SSD1306_WHITE);
  oled.drawLine(x + 2, y + 6, x + 6, y + 1, SSD1306_WHITE);
}

// X mark. Footprint: 7 wide × 6 tall.
static void drawCross(int x, int y) {
  oled.drawLine(x + 0, y + 1, x + 6, y + 6, SSD1306_WHITE);
  oled.drawLine(x + 6, y + 1, x + 0, y + 6, SSD1306_WHITE);
}

// Rotating line spinner through 4 frames. Footprint: 7x7.
static void drawSpinner(int x, int y, uint8_t frame) {
  const int cx = x + 3, cy = y + 3, r = 3;
  switch (frame & 3) {
    case 0: oled.drawLine(cx - r, cy,     cx + r, cy,     SSD1306_WHITE); break;
    case 1: oled.drawLine(cx - r, cy + r, cx + r, cy - r, SSD1306_WHITE); break;
    case 2: oled.drawLine(cx,     cy - r, cx,     cy + r, SSD1306_WHITE); break;
    case 3: oled.drawLine(cx - r, cy - r, cx + r, cy + r, SSD1306_WHITE); break;
  }
}

// 5×5 filled square, drawn only when `on` is true. Caller toggles `on`.
static void drawBlinkSquare(int x, int y, bool on) {
  if (on) oled.fillRect(x, y + 1, 5, 5, SSD1306_WHITE);
}

// ---- Header ----------------------------------------------------------------

static void drawHeader() {
  const auto& st = ClaudeEvents::state();
  if (st.wifi_connected) drawWifi(0, 0);
  if (st.ws_connected)   drawCheck(14, 0);
  else                   drawCross(14, 0);

  const int rx = OLED_WIDTH - 7;
  if (st.working) {
    drawSpinner(rx, 0, spinnerFrame);
  } else {
    const bool on = (millis() / kBlinkPeriodMs) % 2 == 0;
    drawBlinkSquare(rx, 0, on);
  }

  oled.drawFastHLine(0, 9, OLED_WIDTH, SSD1306_WHITE);
}

// ---- Body text / wrapping --------------------------------------------------

// Upper-case short label for a tool name.
static const char* toolLabel(const char* tool) {
  if (!tool || !*tool)               return "";
  if (!strcmp(tool, "Edit"))         return "EDIT";
  if (!strcmp(tool, "MultiEdit"))    return "MEDIT";
  if (!strcmp(tool, "Write"))        return "WRITE";
  if (!strcmp(tool, "Read"))         return "READ";
  if (!strcmp(tool, "NotebookEdit")) return "NBEDT";
  if (!strcmp(tool, "Bash"))         return "BASH";
  if (!strcmp(tool, "BashOutput"))   return "SHOUT";
  if (!strcmp(tool, "KillShell"))    return "KILL";
  if (!strcmp(tool, "Glob"))         return "GLOB";
  if (!strcmp(tool, "Grep"))         return "GREP";
  if (!strcmp(tool, "WebFetch"))     return "FETCH";
  if (!strcmp(tool, "WebSearch"))    return "SEARCH";
  if (!strcmp(tool, "Task"))         return "TASK";
  if (!strcmp(tool, "TodoWrite"))    return "TODOS";
  if (!strcmp(tool, "SlashCommand")) return "CMD";
  if (!strcmp(tool, "ExitPlanMode")) return "PLAN";
  if (!strncmp(tool, "mcp__", 5))    return "MCP";
  return tool;
}

// Hard-wrap at exactly `kCols` — no attempt to break on spaces. Used for
// tool labels + filenames / commands where word boundaries don't help and
// we'd rather show every character than drop mid-filename content.
static void drawBodyHard(const char* text) {
  oled.setCursor(0, kBodyY0);
  if (!text || !*text) return;

  char line[kCols + 1];
  size_t len = strlen(text);

  size_t take = len > kCols ? kCols : len;
  memcpy(line, text, take);
  line[take] = '\0';
  oled.print(line);

  if (len <= kCols) return;
  size_t rem = len - kCols;
  if (rem > kCols) rem = kCols;
  memcpy(line, text + kCols, rem);
  line[rem] = '\0';
  oled.setCursor(0, kBodyY1);
  oled.print(line);
}

// Greedy word-wrap `text` into up to two 21-char rows. Breaks on the last
// space at or before column 21; hard-breaks if no space is available.
static void drawBody(const char* text) {
  oled.setCursor(0, kBodyY0);
  if (!text || !*text) return;

  char line[kCols + 1];
  size_t len = strlen(text);

  // -------- Row 1 --------
  size_t brk = len > kCols ? kCols : len;
  if (len > kCols) {
    for (size_t i = kCols; i > kCols / 2; --i) {
      if (text[i] == ' ') { brk = i; break; }
    }
  }
  memcpy(line, text, brk);
  line[brk] = '\0';
  oled.print(line);

  // -------- Row 2 --------
  if (brk >= len) return;
  size_t start = brk;
  while (text[start] == ' ') ++start;
  size_t rem = strlen(text + start);
  if (rem > kCols) rem = kCols;
  memcpy(line, text + start, rem);
  line[rem] = '\0';
  oled.setCursor(0, kBodyY1);
  oled.print(line);
}

static void drawBody() {
  const auto& st = ClaudeEvents::state();

  char buf[96];
  if (st.pending_permission[0]) {
    const char* label = toolLabel(st.pending_tool);
    if (st.pending_detail[0]) {
      snprintf(buf, sizeof(buf), "ALLOW? %s %s", label, st.pending_detail);
    } else {
      snprintf(buf, sizeof(buf), "ALLOW? %s", label);
    }
    drawBodyHard(buf);
  } else if (st.current_tool[0]) {
    const char* label = toolLabel(st.current_tool);
    if (st.tool_detail[0]) {
      snprintf(buf, sizeof(buf), "%s %s", label, st.tool_detail);
    } else {
      snprintf(buf, sizeof(buf), "%s", label);
    }
    drawBodyHard(buf);
  } else if (st.last_summary[0]) {
    drawBody(st.last_summary);
  } else if (!st.ws_connected) {
    drawBody("waiting for bridge");
  } else {
    drawBody("");
  }
}

// ---- Redraw pacing ---------------------------------------------------------

static uint32_t lastEventSnapshot = 0;
static bool lastWs = false;
static bool lastWifi = false;
static bool lastWorking = false;
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
  lastWs = st.ws_connected;
  lastWifi = st.wifi_connected;
  lastWorking = st.working;
}

// ---- Public API ------------------------------------------------------------

void begin() {
  Wire.begin();
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    LOG_ERR("SSD1306 init failed at 0x%02X", OLED_I2C_ADDR);
    return;
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(F("robot_experiment"));
  oled.setCursor(0, 16);
  oled.print(F("booting..."));
  oled.display();
}

void invalidate() { dirty = true; }

void drawPortalScreen(const char* ssid, const char* ip) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(F("CONFIG MODE"));
  oled.drawFastHLine(0, 9, OLED_WIDTH, SSD1306_WHITE);
  oled.setCursor(0, kBodyY0);
  oled.print(ssid);
  oled.setCursor(0, kBodyY1);
  oled.print(ip);
  oled.display();
}

void tick() {
  const uint32_t now = millis();
  if (now - lastRedraw < kMinRedrawMs) return;

  const auto& st = ClaudeEvents::state();

  // Periodic animation tick: spinner frames while working, blink flips
  // while idle. Both are driven from millis(), not event arrival.
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

  oled.clearDisplay();
  drawHeader();
  drawBody();
  oled.display();
}

}  // namespace Display
