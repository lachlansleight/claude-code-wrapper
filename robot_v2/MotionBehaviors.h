#pragma once

// State-driven arm motion. Reads Personality::current() and issues jogs /
// waggles / thinking-mode toggles on the low-level `Motion` module so the
// arms (driven by a single servo through a gear) move in sympathy with
// whatever mood the robot is in.
//
// Replaces the old AmbientMotion (tool-edge jogs + thinking osc) and
// AttractScheduler (idle waggles). Both of those read ClaudeEvents
// directly; this one goes through Personality so the motion matches the
// same state machine the face is showing.

#include <Arduino.h>

namespace MotionBehaviors {

void begin();
void tick();

}  // namespace MotionBehaviors
