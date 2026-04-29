#pragma once

// Lightweight motion subsystem. Owns the attached servo and exposes
// parameterized primitives (jog / waggle / thinking-mode oscillation /
// hold) on top of which `MotionBehaviors` layers per-state behaviour.
//
// Conventions
// -----------
// All offsets are signed degrees relative to the servo's mechanical
// centre (90°). A positive value swings one way, negative the other.
// Every motion path here clamps to a configurable "safe range"
// (`setSafeRange`) so a typo in the behaviour table can't drive the
// servo into the chassis.

#include <Arduino.h>

namespace Motion {

void begin();
void tick();

// Configure the signed offset range (relative to centre) the servo is
// physically allowed to reach. All `playJog`, `playWaggle`,
// `holdPosition` and thinking-mode writes are clamped to this. Set
// once from `MotionBehaviors::begin()` so the constants live alongside
// the rest of the tuning. Defaults are wide-open until overridden.
void setSafeRange(int8_t minOffsetDeg, int8_t maxOffsetDeg);

// Slew to `90 + offsetDeg` over `durationMs` and hold there. Preempts
// any currently playing pattern (jogs are timely reactions — latency
// matters). Successive jogs interpolate from the last commanded angle,
// so motion is continuous rather than snapping home. `offsetDeg` is
// clamped to the safe range.
void playJog(int8_t offsetDeg, uint16_t durationMs = 250);

// Play a 5-frame waggle pattern: centre, +amp, -amp, +amp, centre.
// Each frame is held for `periodMs / 10`, so the full waggle takes
// `periodMs / 2` and leaves room for an equal pause before the caller
// retriggers (typical use: MotionBehaviors retriggers at intervals of
// `periodMs`). No-op if a pattern or jog is already running, so a
// premature retrigger is harmless. `center` and `amplitude` are
// clamped to the safe range.
void playWaggle(int8_t center, uint8_t amplitude, uint16_t periodMs);

// Cancel any pattern, jog or thinking oscillation. Servo holds at the
// last commanded angle. Use when a state with `MotionMode::NONE` is
// entered and we want to actively still the arm.
void cancelAll();

// Smooth sine oscillation around `90 + centerOffset` with ±`amplitude`
// degrees and full period `periodMs`. On enable, the base angle eases
// from wherever the servo currently sits to the new centre over the
// first second, and amplitude ramps in over the same window so the
// transition is smooth. Patterns, jogs and holds all preempt. Pass
// `on=false` to disable; the other args are ignored in that case.
void setThinkingMode(bool on, int8_t centerOffset = 0,
                     uint8_t amplitude = 5, uint16_t periodMs = 2000);

// Override mode: slew to `90 + offsetDeg` and lock the servo there for
// `durationMs`. Suppresses patterns, jogs and thinking-mode oscillation
// for the duration. After it expires, `consumeHoldExpired()` returns
// true once so the caller (MotionBehaviors) can re-establish the
// state-driven motion. `offsetDeg` is clamped to the safe range.
void holdPosition(int8_t offsetDeg, uint32_t durationMs);

// One-shot edge: returns true the first time it's called after a hold
// has just elapsed. Used by MotionBehaviors to re-enter the current
// personality state and resume normal arm motion.
bool consumeHoldExpired();

bool isBusy();

}  // namespace Motion
