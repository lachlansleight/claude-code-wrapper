#pragma once

#include <Arduino.h>

#include "../face/SceneTypes.h"

/**
 * @file MotionBehaviors.h
 * @brief Expression → arm-motion choreography table.
 *
 * MotionBehaviors maps a `Face::Expression` to a motion *mode* (static
 * pose, random drift, oscillation, waggle, thinking sine, or none) and
 * a set of parameters (centre offset, amplitude, period, slew, jitter).
 * The mapping lives in a single static table indexed by the
 * `Face::Expression` enum order — adding/removing expressions there
 * requires updating that table (a `static_assert` in the .cpp enforces
 * matching size).
 *
 * On each tick(expression):
 *  - If the expression has changed since the last tick, run an
 *    `onEnter` for the new mode (kicks off the first jog/pattern/etc.).
 *  - If a Motion hold has just expired, re-enter to restart motion.
 *  - Otherwise run `onDuring`, which schedules the next cycle (e.g.
 *    flips an oscillator, picks a new drift target).
 *
 * Centre offsets here are constrained to ±45° via Motion::setSafeRange.
 * The face uses periodMsFor() to body-bob in time with the arm.
 */
namespace MotionBehaviors {

/**
 * Tighten the servo safe range to ±45° (the mechanical safe envelope
 * for the current arm) and reset internal expression-tracking state.
 * Call once during setup, after Motion::begin().
 */
void begin();

/**
 * Drive the servo for the supplied effective expression. Edge-detects
 * expression changes to call `onEnter`, polls Motion::consumeHoldExpired
 * to recover from holds, and otherwise advances the active periodic
 * cycle. Intended to be called every loop with the result of
 * EmotionSystem + VerbSystem composition.
 */
void tick(Face::Expression expression);

/**
 * Period in ms for the arm motion attached to @p expression, or 0 if
 * the motion mode is aperiodic (NONE, STATIC, RANDOM_DRIFT). Read by
 * FrameController to body-bob the face in lockstep with the arm —
 * change a state's `periodMs` in the kMotion[] table and the face
 * auto-resyncs.
 */
uint16_t periodMsFor(Face::Expression expression);

}  // namespace MotionBehaviors
