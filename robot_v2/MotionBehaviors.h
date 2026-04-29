#pragma once

// State-driven arm motion. Reads Personality::current() and issues jogs /
// waggles / thinking-mode toggles on the low-level `Motion` module so the
// arms (driven by a single servo through a gear) move in sympathy with
// whatever mood the robot is in.
//
// All tuning lives in MotionBehaviors.cpp — one row per personality state.

#include <Arduino.h>

#include "Personality.h"

namespace MotionBehaviors {

void begin();
void tick();

// Period (in ms) of the arm motion for `s`, as configured in the per-
// state table. Returns 0 for states with no rhythmic motion (NONE,
// STATIC). Used by FrameController to sync face animation to the arm
// (e.g. breathing in SLEEP, body-bob in EXECUTING/FINISHED).
uint16_t periodMsFor(Personality::State s);

}  // namespace MotionBehaviors
