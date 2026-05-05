#pragma once

#include <Arduino.h>

/**
 * @file EmotionSystem.h
 * @brief Continuous valence/arousal model with hysteresis-snapped emotion regions.
 *
 * EmotionSystem owns the *how it feels*. It maintains a 2D continuous
 * state — `valence` in [-1, +1] (negative→positive) and `activation`
 * in [0, 1] (calm→excited) — that decays toward driver targets via a
 * first-order low-pass each tick. The plane is partitioned into
 * axis-aligned rectangles (one per `NamedEmotion`); the state is
 * "snapped" to whichever region contains it, with hysteresis so the
 * snap doesn't chatter near boundaries.
 *
 * ## Inputs
 *
 *  - **Impulses** — instantaneous deltas applied to the raw state.
 *    Used for one-shot reactions.
 *  - **Held drivers** — long-running pulls toward a target valence.
 *    Each driver is keyed by a stable id (see `Drivers` namespace).
 *    Multiple drivers can be active; the one with the largest
 *    |target| wins. Setting a target with the same id re-targets;
 *    releasing clears the slot.
 *  - **Direct setters** — setValence/setArousal etc. for the bridge
 *    test path.
 *
 * Activation always relaxes toward 0; valence relaxes toward the
 * winning held-driver target (or 0 if none).
 *
 * ## Time constants
 * Activation τ ≈ 6 s, valence τ ≈ 90 s — emotion shifts are slow on
 * purpose; verb changes are the fast layer.
 *
 * ## Snap hysteresis
 * The current snap survives until a *different* region's centre is
 * meaningfully closer (Δdist > 0.05) for at least 100 ms — prevents
 * flapping when the raw point sits on a boundary.
 */
namespace EmotionSystem {

/// Coarse named emotion regions. Each maps to a face/colour scheme.
enum class NamedEmotion : uint8_t {
  Neutral = 0,  ///< Centre band. Default.
  Happy,        ///< Mid-valence positive (lower arousal half).
  Excited,      ///< High valence + high arousal.
  Joyful,       ///< Top-right: very high arousal + valence.
  Sad,          ///< Negative valence (full arousal range).
  Count
};

/// Raw continuous (valence, activation) point.
struct Emotion {
  float valence;     ///< [-1, +1].
  float activation;  ///< [0, 1].
};

/// Raw point plus the currently-snapped region (with hysteresis applied).
struct SnappedEmotion {
  NamedEmotion named;
  float valence;
  float activation;
};

/// Maximum number of concurrent held valence drivers.
static constexpr uint8_t kMaxHeldDrivers = 8;

/// One slot in the held-driver table. @see setHeldTarget.
struct HeldValenceDriver {
  uint8_t id;
  float targetValence;
};

/// Snapshot of internal state for diagnostic rendering.
struct DebugState {
  NamedEmotion snappedCurrent;
  NamedEmotion snappedPending;            ///< Region we'd snap to once hysteresis clears.
  bool pendingSnapActive;
  uint32_t pendingSnapSinceMs;
  uint8_t heldDriverCount;
  HeldValenceDriver heldDrivers[kMaxHeldDrivers];
};

/**
 * Stable driver id constants used by EventRouter. New drivers should
 * pick a unique non-zero id and be documented here.
 */
namespace Drivers {
/// Pulled while a permission prompt is open (negative valence).
static constexpr uint8_t PendingPermission = 1;
/// Pulled while the verb is Straining.
static constexpr uint8_t Straining = 2;
}

/// Zero raw state, drivers, and snap. Call once in setup().
void begin();

/**
 * Advance the low-pass filter toward the active driver target and
 * update the snapped region with hysteresis. Must be called every
 * loop. No-op if dt == 0.
 */
void tick();

/// Apply instantaneous deltas (clamped). Use for one-shot reactions.
void impulse(float dValence, float dActivation);

/// Force-set valence to @p value (clamped to [-1, +1]).
void setValence(float value);
/// Force-set arousal to @p value (clamped to [0, 1]).
void setArousal(float value);
/// `setValence(currentValence + delta)`.
void modifyValence(float delta);
/// `setArousal(currentActivation + delta)`.
void modifyArousal(float delta);

/**
 * Add or update a held valence driver keyed by @p driverId. While
 * active, the raw valence relaxes toward the *largest-|target|*
 * driver. Calling again with the same id retargets the existing slot.
 * Silently dropped if all `kMaxHeldDrivers` slots are full.
 */
void setHeldTarget(uint8_t driverId, float targetValence);
/// Clear the slot held by @p driverId. No-op if none.
void releaseHeldTarget(uint8_t driverId);

/// Current raw (valence, activation).
Emotion raw();
/// Current raw point + the snapped NamedEmotion with hysteresis applied.
SnappedEmotion snapped();
/// Snapshot for debug overlays.
DebugState debugState();

/// Stable lowercase string for @p e ("happy", "joyful", ...).
const char* emotionName(NamedEmotion e);

}  // namespace EmotionSystem
