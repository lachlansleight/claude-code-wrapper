#include "Display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <stdarg.h>
#include <string.h>

#include "ClaudeEvents.h"
#include "DebugLog.h"
#include "config.h"

namespace Display {

// 6x8 glyphs at size 1 → 21 visible columns, 4 rows on a 128×32 panel.
static constexpr uint8_t kCols = 21;
static constexpr uint8_t kRows = 4;
static constexpr uint8_t kLogRows = 2;
static constexpr uint32_t kMinRedrawMs = 50;

static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// Small ring buffer for the scrolling log region.
static char logBuf[kLogRows][kCols + 1] = {};
static uint8_t logHead = 0;  // index of the most-recent line

static bool dirty = true;
static uint32_t lastRedraw = 0;

// Snapshot used to detect state changes without redrawing every tick.
static uint32_t lastEventSnapshot = 0;
static bool lastWs = false;
static bool lastWifi = false;
static bool lastWorking = false;
static char lastHook[24] = {};
static char lastPerm[8] = {};

static void truncateInto(char* dst, size_t cap, const char* src) {
  if (!src) { dst[0] = '\0'; return; }
  size_t len = strlen(src);
  if (len >= cap) len = cap - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

void begin() {
  Wire.begin();  // default SDA=21, SCL=22 on ESP32
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

void log(const char* line) {
  logHead = (logHead + 1) % kLogRows;
  truncateInto(logBuf[logHead], sizeof(logBuf[logHead]), line ? line : "");
  dirty = true;
}

void logf(const char* fmt, ...) {
  char tmp[kCols + 1];
  va_list args;
  va_start(args, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, args);
  va_end(args);
  log(tmp);
}

void invalidate() { dirty = true; }

static const char* shortSession(const char* s) {
  // Show the last 4 chars of the session id. Session ids can be UUIDs which
  // are wildly too long for a 21-char line.
  if (!s || !*s) return "----";
  size_t n = strlen(s);
  if (n <= 4) return s;
  return s + n - 4;
}

static void render() {
  const auto& st = ClaudeEvents::state();
  oled.clearDisplay();
  oled.setCursor(0, 0);

  // Row 0: status bar — W (wifi) | B (bridge) | working/idle | session tail
  char bar[kCols + 1];
  snprintf(bar, sizeof(bar), "%c%c %s %s",
           st.wifi_connected ? 'W' : '-',
           st.ws_connected   ? 'B' : '-',
           st.working        ? "WORK" : "idle",
           shortSession(st.session_id));
  oled.setCursor(0, 0);
  oled.print(bar);

  // Row 1: context — pending permission wins, else last hook/tool.
  char ctx[kCols + 1];
  if (st.pending_permission[0]) {
    // request_id is always 5 chars, leave 14 for tool name after "?id ".
    snprintf(ctx, sizeof(ctx), "?%.5s %.14s",
             st.pending_permission, st.pending_tool);
  } else if (st.last_hook[0]) {
    if (st.last_tool[0]) {
      // Budget 21 chars across "hook:tool" — 10 each + colon.
      snprintf(ctx, sizeof(ctx), "%.10s:%.10s", st.last_hook, st.last_tool);
    } else {
      snprintf(ctx, sizeof(ctx), "%.*s", (int)(sizeof(ctx) - 1), st.last_hook);
    }
  } else if (!st.ws_connected) {
    snprintf(ctx, sizeof(ctx), "waiting for bridge");
  } else {
    snprintf(ctx, sizeof(ctx), "(no activity)");
  }
  oled.setCursor(0, 8);
  oled.print(ctx);

  // Rows 2-3: log ring, oldest-first so newest sits on the bottom row.
  for (uint8_t i = 0; i < kLogRows; ++i) {
    uint8_t idx = (logHead + 1 + i) % kLogRows;
    oled.setCursor(0, 16 + 8 * i);
    oled.print(logBuf[idx]);
  }

  oled.display();
}

static bool stateChanged() {
  const auto& st = ClaudeEvents::state();
  if (st.last_event_ms != lastEventSnapshot) return true;
  if (st.ws_connected   != lastWs)          return true;
  if (st.wifi_connected != lastWifi)        return true;
  if (st.working        != lastWorking)     return true;
  if (strcmp(st.last_hook, lastHook) != 0)  return true;
  if (strcmp(st.pending_permission, lastPerm) != 0) return true;
  return false;
}

static void snapshot() {
  const auto& st = ClaudeEvents::state();
  lastEventSnapshot = st.last_event_ms;
  lastWs = st.ws_connected;
  lastWifi = st.wifi_connected;
  lastWorking = st.working;
  strncpy(lastHook, st.last_hook, sizeof(lastHook));
  lastHook[sizeof(lastHook) - 1] = '\0';
  strncpy(lastPerm, st.pending_permission, sizeof(lastPerm));
  lastPerm[sizeof(lastPerm) - 1] = '\0';
}

void tick() {
  const uint32_t now = millis();
  if (now - lastRedraw < kMinRedrawMs) return;
  if (!dirty && !stateChanged()) return;
  lastRedraw = now;
  dirty = false;
  snapshot();
  render();
}

}  // namespace Display
