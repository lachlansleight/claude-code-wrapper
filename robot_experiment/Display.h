#pragma once

// OLED UI for the 128x32 SSD1306.
//
// Layout (4 rows × 21 chars at text size 1):
//   row 0: status bar   — WiFi + bridge conn + working/idle + session tail
//   row 1: context line — current hook / tool, or pending permission
//   row 2: log line -1
//   row 3: log line 0   (most recent)
//
// Log lines come from `Display::log()`. The display is only redrawn when
// something changed AND at most every ~50ms, so it's safe to call from
// event handlers.

#include <Arduino.h>

namespace Display {

void begin();

// Push a short message onto the scrolling log (bottom two rows).
void log(const char* line);
void logf(const char* fmt, ...);

// Mark the rendered-state as dirty. Call after updating ClaudeState so the
// status bar re-renders on the next tick().
void invalidate();

// Call from loop(); rate-limits actual redraws internally.
void tick();

}  // namespace Display
