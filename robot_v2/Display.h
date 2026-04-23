#pragma once

// UI for the 240x240 round GC9A01 TFT (RGB565), driven via TFT_eSPI with a
// full-screen sprite framebuffer pushed over DMA each frame.
//
// Layout (inside a 100px-radius safe circle = 20px chord padding from rim):
//   y ~24-40   header — three 16x16 icons centred horizontally:
//                wifi (white if connected, dim if not)
//                bridge connection (cyan check / yellow cross)
//                working spinner / idle blink dot
//   y ~46      faint chord separator under the header
//   y 60-220   body text — default font size 2, word-wrapped per-row to
//              the chord width at that row's vertical centre, each line
//              centred horizontally
//
// Body priority (same as the SSD1306 version):
//   1. Pending permission: "ALLOW? TOOL detail"
//   2. Working + current tool: "TOOL detail"
//   3. Idle: last assistant summary
//
// The display reads ClaudeEvents::state() and redraws itself. Nothing else
// needs to call into Display except begin() / tick(). invalidate() forces
// a redraw on the next tick (e.g. after a manual state poke).
//
// setBrightness() drives the backlight via LEDC PWM on TFT_BL. No-op if
// TFT_BL isn't defined in User_Setup.h.

#include <Arduino.h>

namespace Display {

void begin();
void invalidate();
void tick();

// 0-100. Defaults to 100 on boot. No auto-dim behaviour wired in yet.
void setBrightness(uint8_t pct);

// One-shot screen for the provisioning portal. Bypasses the state-driven
// renderer; the firmware's main loop is blocked while the portal runs.
void drawPortalScreen(const char* ssid, const char* ip);

}  // namespace Display
