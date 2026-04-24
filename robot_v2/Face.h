#pragma once

// Procedural face renderer. Reads Personality::current() each tick and
// draws the corresponding static face frame into the Display sprite,
// then asks Display to DMA-push it.
//
// v1: static frames per state. No tweening, no per-frame animation.
// Redraws only when the personality state changes. When we add animation
// later, the redraw decision + frame function both grow — the module
// surface stays the same.
//
// Art direction (placeholder — will be revisited for aesthetics):
//   - Monochrome white on black
//   - Two ring-outline eyes with solid pupil discs
//   - Simple line/curve mouth
//   - Everything centred inside the 240x240 panel

#include <Arduino.h>

namespace Face {

void begin();
void tick();

// Force a redraw on the next tick (e.g. after returning from the portal).
void invalidate();

}  // namespace Face
