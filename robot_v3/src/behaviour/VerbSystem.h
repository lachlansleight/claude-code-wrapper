#pragma once

#include <Arduino.h>

/**
 * @file VerbSystem.h
 * @brief Discrete state machine: "what is the robot doing right now?"
 *
 * VerbSystem owns the *what*. It is one of the two behaviour systems
 * the firmware composes (the other being EmotionSystem, which owns the
 * *how it feels*). Each verb maps to a face animation and an arm
 * motion behaviour via Face::Expression.
 *
 * ## Layered semantics
 *
 *  - **Current verb** — the steady-state activity. Set directly by
 *    EventRouter from agent_event activity flow (e.g. tool started →
 *    Reading, file write → Writing, shell exec → Executing).
 *  - **Linger** — when an activity finishes, the caller can armLinger()
 *    a window during which the verb stays put before it auto-decays
 *    to Thinking. Prevents flicker between rapid same-type tools and
 *    "Thinking" gaps.
 *  - **Strain promotion** — Executing held continuously for ≥5 s is
 *    automatically promoted to Straining (the "this is taking a
 *    while" pose).
 *  - **Overlay** — short, transient verbs (Waking, AttractingAttention)
 *    that run on top of the current state for a fixed duration via
 *    fireOverlay(). Pre-empt the current verb's display while active,
 *    then snap back. If a second overlay fires while one is running,
 *    it queues and runs after.
 *
 * ## Sleeping
 * `Sleeping` is the boot/idle state. Re-entered by the router when no
 * agent activity is seen for a long while; cleared by `Waking` overlay
 * when activity resumes.
 *
 * ## Effective verb
 * Renderers should always read effective(), not current(): it folds
 * the active overlay (if any) on top.
 */
namespace VerbSystem {

/**
 * The set of verbs the firmware understands. Two of these — Waking and
 * AttractingAttention — are **overlay-only**: setVerb() with one of
 * them is rerouted into a 1 s overlay rather than a state change.
 */
enum class Verb : uint8_t {
  None = 0,            ///< No active verb (post-clear default).
  Thinking,            ///< Default "alive but not actively doing" pose.
  Reading,
  Writing,
  Executing,
  Straining,           ///< Auto-promoted from Executing after 5 s.
  Sleeping,            ///< Long idle / boot state.
  Waking,              ///< Overlay: brief wake animation.
  AttractingAttention, ///< Overlay: pulse for permission requests etc.
  Count
};

/**
 * Snapshot of internal state for diagnostic / debug rendering.
 * Mirrors the private statics in VerbSystem.cpp; not meant for
 * behavioural logic.
 */
struct DebugState {
  Verb current;                     ///< Underlying verb (overlay ignored).
  Verb effective;                   ///< Overlay if active else current.
  Verb overlayVerb;                 ///< The active overlay verb, if any.
  Verb preOverlayVerb;              ///< What we'll restore to when overlay ends.
  Verb queuedOverlayVerb;           ///< Pending overlay (queued while another runs).
  bool overlayActive;
  bool overlayQueued;
  uint32_t enteredAtMs;             ///< millis() when current became current.
  uint32_t lingerUntilMs;           ///< 0 if no linger armed.
  uint32_t overlayUntilMs;          ///< Expiry of active overlay.
  uint32_t queuedOverlayDurationMs;
};

/// Reset state and start in `Sleeping`. Call once in setup().
void begin();

/**
 * Service overlays, lingers and the Executing→Straining auto-promotion.
 * Must be called every loop. No-op if nothing is timed.
 */
void tick();

/**
 * Set the current verb. Passing `Verb::None` calls clearVerb(). Passing
 * an overlay verb (Waking / AttractingAttention) is treated as
 * `fireOverlay(v, 1000)` — overlays cannot be "current".
 */
void setVerb(Verb v);

/// Reset to `Verb::None` and cancel any armed linger. Overlays unaffected.
void clearVerb();

/**
 * Hold the current verb for @p ms before auto-decaying to `Thinking`.
 * Pass 0 to cancel a previously-armed linger. Used by EventRouter to
 * smooth over rapid activity churn (read tools usually finish
 * instantly; lingering avoids strobe).
 */
void armLinger(uint32_t ms);

/**
 * Run @p overlayVerb (must be an overlay verb) for @p durationMs
 * milliseconds, after which the previous current verb is restored. If
 * an overlay is already running, the new request is queued and fires
 * when the running one ends. Only one queue slot — re-firing
 * overwrites the queued entry.
 */
void fireOverlay(Verb overlayVerb, uint32_t durationMs);

/**
 * Variant: when the overlay ends, restore to @p postOverlayVerb
 * instead of whatever was current at fire time. Lets the router
 * cleanly change the underlying verb behind a wake animation (e.g.
 * sleep → Waking overlay → Thinking).
 */
void fireOverlay(Verb overlayVerb, uint32_t durationMs, Verb postOverlayVerb);

/// The underlying verb, ignoring any active overlay.
Verb current();
/// The verb to actually render — overlay if active, else current().
Verb effective();
/// True while an overlay is running.
bool overlayActive();
/// `millis()` snapshot when the current verb was entered (overlays don't change this).
uint32_t enteredAtMs();
/// Convenience: `millis() - enteredAtMs()`.
uint32_t timeInCurrentMs();
/// Snapshot for debug overlays / serial dumps.
DebugState debugState();

/// Stable lowercase string for @p v ("thinking", "executing", ...). Returns "?" for unknown.
const char* verbName(Verb v);

/**
 * Case-insensitive parser for the names produced by verbName(). Also
 * accepts `attractingattention` as a no-underscore alias. Returns true
 * and writes @p outVerb on success; false otherwise.
 */
bool parseVerb(const char* text, Verb* outVerb);

}  // namespace VerbSystem
