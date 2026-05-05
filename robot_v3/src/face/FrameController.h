#pragma once

#include <Arduino.h>

#include "SceneTypes.h"

/**
 * @file FrameController.h
 * @brief Per-frame face-rendering orchestrator: tweening, blinks, gaze, body bob.
 *
 * FrameController is the glue between the static `kBaseTargets[]`
 * geometry table (one FaceParams per Expression) and the actual
 * rendered scene. It owns all the *animation* state — the systems
 * below it are stateless renderers, the system above it
 * (`SceneContextFill`) just snapshots the world.
 *
 * Per-frame responsibilities, in order:
 *  - Detect Expression changes and start a 250 ms eased tween from
 *    the current pose to the new target.
 *  - Maintain the blink schedule (close 80 ms, open 130 ms; period
 *    randomised per expression).
 *  - Drive the periodic "thinking head-tilt flip" — every 3–6 s the
 *    sign of `face_rot` and `pupil_dx` is smoothly inverted, so the
 *    head sways back and forth while thinking.
 *  - Compute a body-bob offset for body-bobbing expressions, in
 *    lockstep with `MotionBehaviors::periodMsFor(expr)` so the face
 *    syncs visually with the arm.
 *  - Add a per-expression gaze offset (idle micro-glances during
 *    Neutral; small repeating motion otherwise).
 *  - Smooth the mood-ring colour (200 ms τ low-pass) and the
 *    read/write stream-effect alphas (100 ms τ).
 *  - Detect Settings::settingsVersion() changes and immediately
 *    re-bake the mood-ring colour from the palette.
 *  - Finally call Scene::renderScene or TextScene::renderTextScene
 *    and Display::pushFrame.
 *
 * The tick cadence runs at ~30 Hz (33 ms) by default, dropping to
 * ~60 Hz (16 ms) while a stream effect is fading or a stream-effect
 * verb is active, to keep the scrolling smooth.
 */
namespace Face {

/**
 * Initialise tween state, seed the RNG from `esp_random`, snap the
 * mood ring to the VerbSleeping pose, and cache the current
 * Settings::settingsVersion(). Call once in setup() after
 * Settings::begin() and Display::begin().
 */
void begin();

/**
 * Render at most one frame. Cheap to call every loop — the tick
 * itself is internally throttled to the per-frame interval. No-op if
 * Display::ready() is false. Reads palette via Settings.
 */
void tick(const SceneContext& ctx);

/**
 * Force the next tick to fire immediately by resetting the
 * throttle. Used after a state-changing operation (e.g. mode toggle)
 * to make the change visible without waiting for the next interval.
 */
void invalidate();

}  // namespace Face
