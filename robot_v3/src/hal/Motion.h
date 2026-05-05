#pragma once

#include <Arduino.h>

/**
 * @file Motion.h
 * @brief Single-channel hobby servo driver with eased jogs, pattern
 *        playback, drifting "thinking" mode and timed holds.
 *
 * The robot has a single SG92R-class servo driving the arm. Angles in
 * this API are **offsets from centre (90°)** in degrees, where the safe
 * range is configurable via setSafeRange() (default ±90, the firmware
 * tightens to ±45 in MotionBehaviors::begin). Internally the servo is
 * always written as an absolute 0..180.
 *
 * Three layered playback systems share the channel, in priority order:
 *  1. **holdPosition** — locks the servo to a target for a fixed
 *     duration; jog still slews under the hold. Used by Joyful/Wakeup
 *     to keep the servo planted while the face animates.
 *  2. **playJog** — single eased move (smoothstep) to a target offset
 *     over a duration. Pre-empts any active pattern.
 *  3. **playWaggle** / **setThinkingMode** — periodic motion. Waggle is
 *     a fixed 5-frame keyframe pattern; thinking is a continuous sine
 *     drift around a centre with ease-in.
 *
 * Motion::tick() must be called every loop; everything else is
 * non-blocking and edge-triggered. Higher-level expression-driven
 * choreography lives in MotionBehaviors.
 */
namespace Motion {

/**
 * Attach the servo on `SERVO_PIN` (from `config.h`) at 50 Hz with a
 * 500–2400 µs pulse range and snap to centre. Logs `LOG_ERR` if the
 * channel cannot be claimed; subsequent calls are no-ops in that case.
 */
void begin();

/**
 * Advance the active jog/pattern/thinking state by one slice. Must be
 * called every loop. Resolves expired holds, slews active jogs by 20 ms
 * cadence, advances pattern keyframes when their dwell elapses, and
 * drives the sine drift in thinking mode. No-op if begin() failed.
 */
void tick();

/**
 * Clamp future targets to `[minOffsetDeg, maxOffsetDeg]` (auto-swapped
 * if reversed, hard-clamped to ±90). Used to prevent the firmware from
 * commanding mechanically unsafe angles. Existing in-flight motion is
 * not retro-clamped.
 */
void setSafeRange(int8_t minOffsetDeg, int8_t maxOffsetDeg);

/**
 * Smoothstep-eased move from the current commanded angle to
 * `offsetDeg` over `durationMs`. Cancels any active pattern. Issues
 * intermediate writes at ~50 Hz.
 */
void playJog(int8_t offsetDeg, uint16_t durationMs = 250);

/**
 * Play a 5-frame waggle (centre, +amp, -amp, +amp, centre), with each
 * keyframe held for `periodMs/10`. Total length is roughly half of
 * `periodMs`. Ignored if a pattern or jog is already active.
 */
void playWaggle(int8_t center, uint8_t amplitude, uint16_t periodMs);

/**
 * Drop everything: pattern, jog and thinking mode all stop. Holds are
 * **not** cleared (use the natural expiry path or pre-empt with another
 * holdPosition/jog).
 */
void cancelAll();

/**
 * Continuous low-amplitude sine drift around `centerOffset`, fading in
 * over ~1 s. Pass `on=false` to disable. Used by VerbThinking to give
 * the arm a subtle alive-feeling sway. Compatible with concurrent jogs
 * (the jog wins for its duration, thinking resumes after).
 */
void setThinkingMode(bool on, int8_t centerOffset = 0, uint8_t amplitude = 5,
                     uint16_t periodMs = 2000);

/**
 * Slew to `offsetDeg` over a 250 ms eased jog and lock the servo there
 * for `durationMs`. While the hold is active, only the jog continues;
 * patterns and thinking are suppressed. When the hold expires it sets
 * an internal edge flag that consumeHoldExpired() reads.
 */
void holdPosition(int8_t offsetDeg, uint32_t durationMs);

/**
 * Read-and-clear the "hold just expired" edge. MotionBehaviors uses
 * this to re-enter the current expression's motion mode after a hold
 * unlocks the channel, so the arm doesn't sit frozen.
 */
bool consumeHoldExpired();

/**
 * True if any of pattern, jog, or hold is active. (Thinking mode is
 * **not** counted as busy — it is considered a passive ambient drift.)
 */
bool isBusy();

}  // namespace Motion
