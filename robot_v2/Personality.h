#pragma once

// High-level personality state machine.
//
// Sits between ClaudeEvents (raw bridge hooks + polled state) and the
// expressive modules (Face, Motion behaviours). Exposes one answer to
// "what's the robot doing right now?" that renderers can dispatch on.
//
// High-level states:
//
//   idle / thinking / reading / writing / executing / executingLong /
//   finished / ready / waking / sleep / blocked
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
  EXECUTING,
  EXECUTING_LONG,
  FINISHED,
  EXCITED,    // 10s post-finished, big smile + arm oscillation
  READY,      // 60s after EXCITED, calmer smile + occasional waggle
  WAKING,
  SLEEP,
  BLOCKED,    // awaiting permission verdict — sad-face cousin of EXCITED
  WANTS_ATTENTION,  // 1s startle to flag the user; decays to whatever was current on entry
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
