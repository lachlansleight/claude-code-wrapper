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
 * The mapping lives in `FaceConfig::kMotion` (`FACE_CONFIG_DATA.h`),
 * indexed by `Face::Expression` order (`static_assert` there enforces
 * row count).
 *
 * On each tick(ctx):
 *  - Emotion expressions (Neutral … Disappointed) drive the continuous
 *    blended arm layer via ctx.base_emotion_arm; the kMotion table is
 *    not used for those.
 *  - Verbs/overlays use kMotion: on expression change run `onEnter`, poll
 *    hold expiry, then `onDuring` for periodic verb cycles.
 *
 * Centre offsets here are constrained to ±45° via Motion::setSafeRange.
 * The face uses periodMsForContext() / periodMsFor() to body-bob with
 * the arm.
 */
namespace MotionBehaviors {

/**
 * Tighten the servo safe range to ±45° (the mechanical safe envelope
 * for the current arm) and reset internal expression-tracking state.
 * Call once during setup, after Motion::begin().
 */
void begin();

/**
 * Drive the servo from the filled scene context (verbs use kMotion;
 * idle emotions use blended base_emotion_arm).
 */
void tick(const Face::SceneContext& ctx);

/**
 * Period in ms for the arm motion attached to @p expression, or 0 if
 * the motion mode is aperiodic (NONE, STATIC, RANDOM_DRIFT). Read by
 * FrameController to body-bob the face in lockstep with the arm —
 * change a state's `period_ms` in `FaceConfig::kMotion` and the face
 * auto-resyncs.
 */
uint16_t periodMsFor(Face::Expression expression);

uint16_t periodMsForContext(const Face::SceneContext& ctx);

}  // namespace MotionBehaviors
