#pragma once

// High-level personality state machine.
//
// Sits between ClaudeEvents (raw bridge hooks + polled state) and the
// expressive modules (Face, Motion behaviours). Exposes one answer to
// "what's the robot doing right now?" that renderers can dispatch on.
//
// Eight states as specced in PERSONALITY_PLAN.md:
//
//   idle / thinking / reading / writing / finished / ready / waking / sleep
//
// v1 wires up idle + thinking only — the state table knows about the rest
// so adding them later is one row + one event-handler clause.
//
// All timing knobs live in kStates[] at the top of Personality.cpp. Tweak
// there and reflash.

#include <Arduino.h>

namespace Personality {

enum State : uint8_t {
  IDLE = 0,
  THINKING,
  READING,
  WRITING,
  FINISHED,
  READY,
  WAKING,
  SLEEP,
  kStateCount,
};

void begin();
void tick();

State        current();
uint32_t     enteredAtMs();    // millis() when current state was entered
uint32_t     timeInStateMs();  // millis() - enteredAtMs(), clamped non-neg
const char*  stateName(State s);

// Force a state change from outside (diagnostics / testing). Goes through
// the same min-window / queue logic as event-driven requests.
void request(State target);

}  // namespace Personality
