#pragma once

// OLED UI for the 128x32 SSD1306.
//
// Layout:
//   y 0-7   header icons (wifi + bridge check/cross + working/idle indicator)
//   y 9     1-pixel separator line
//   y 10-15 gap
//   y 16-23 body line 1
//   y 24-31 body line 2
//
// Body priority:
//   1. Pending permission: "ALLOW? TOOL detail"
//   2. Working + current tool: "TOOL detail"
//   3. Idle: last assistant summary (word-wrapped across both lines)
//
// The display pulls straight from ClaudeEvents::state() and redraws itself.
// Nothing else in the firmware needs to call into Display except begin() and
// tick(). invalidate() exists for edge cases (e.g. a manual display test).

#include <Arduino.h>

namespace Display {

void begin();
void invalidate();
void tick();

// One-shot screen for the provisioning portal. Bypasses the state-driven
// renderer; the firmware's main loop is blocked while the portal runs.
void drawPortalScreen(const char* ssid, const char* ip);

}  // namespace Display
