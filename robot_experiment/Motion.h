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

bool isBusy();

}  // namespace Motion
