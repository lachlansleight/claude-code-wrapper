#pragma once

// Lightweight motion subsystem. Owns the attached servo(s) and plays
// non-blocking keyframe patterns on them. Designed so new patterns can be
// added by dropping another `Pattern` constant and a `play*()` wrapper.

#include <Arduino.h>

namespace Motion {

void begin();
void tick();

// Built-in pattern — small left/right waggle around centre. Non-blocking.
// If a pattern is already playing, this one is queued (depth 1: a second
// call while already queued replaces the first).
void playWaggle();

// Slew to `90 + offsetDeg` over ~250ms and hold there until the next jog
// or pattern takes over. Preempts any currently playing pattern (jogs are
// tool-use reactions — latency matters). `offsetDeg` is signed; ±5..±15
// is the intended range. Successive jogs interpolate from the last
// commanded angle, so motion is continuous rather than snapping home.
void playJog(int8_t offsetDeg);

// When enabled AND no pattern is playing, the servo slowly oscillates
// around 90° (±5°, 2 s period). On enable, the base angle eases from
// wherever the servo currently sits back to centre over the first second
// (and the oscillation amplitude ramps in over the same window) so it
// doesn't snap. Patterns (waggle, jog) preempt.
void setThinkingMode(bool on);

bool isBusy();

}  // namespace Motion
