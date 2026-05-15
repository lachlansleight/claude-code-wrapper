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
 *  - **Transient overlay** — any verb (via `fireOverlay()`) plays for a
 *    fixed duration with timeline cross-fade in/out. A second
 *    `fireOverlay()` replaces the in-flight one (previous post discarded).
 *
 * ## Sleeping
 * `Sleeping` is the boot/idle state. Re-entered when a session ends;
 * `Waking` is often played as a transient overlay when activity resumes.
 *
 * ## Effective verb
 * Renderers should always read effective(), not current(): during a
 * transient overlay it returns the overlay verb until the blend-out
 * window, then the post verb.
 */
#include "../face/VerbEnum.generated.h"

namespace VerbSystem {

struct DebugState {
  Verb current;
  Verb effective;
  Verb overlayVerb;
  Verb preOverlayVerb;
  Verb queuedOverlayVerb;
  bool overlayActive;
  bool overlayQueued;
  uint32_t enteredAtMs;
  uint32_t lingerUntilMs;
  uint32_t overlayUntilMs;
  uint32_t queuedOverlayDurationMs;
};

void begin();

void tick();

void setVerb(Verb v);

void clearVerb();

void armLinger(uint32_t ms);

void fireOverlay(Verb overlayVerb, uint32_t durationMs);

void fireOverlay(Verb overlayVerb, uint32_t durationMs, Verb postOverlayVerb);

Verb current();

Verb effective();

bool overlayActive();

uint32_t enteredAtMs();

uint32_t timeInCurrentMs();

uint32_t timeInEffectiveMs();

uint32_t effectiveEnteredAtMs();

DebugState debugState();

const char* verbName(Verb v);

bool parseVerb(const char* text, Verb* outVerb);

}  // namespace VerbSystem
