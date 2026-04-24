#include "Personality.h"

#include <string.h>

#include "ClaudeEvents.h"
#include "DebugLog.h"

namespace Personality {

// ---- Tunables -------------------------------------------------------------
//
// Everything time-based lives here. Tweak and reflash.

struct StateConfig {
  const char* name;
  uint32_t    min_ms;      // transitions requested before this are queued
  uint32_t    max_ms;      // auto-transition after this (0 = never)
  State       on_timeout;
};

static const StateConfig kStates[kStateCount] = {
  // State      name          min_ms    max_ms           on_timeout
  /* IDLE     */ { "idle",         0,    30u * 60 * 1000, SLEEP    },
  /* THINKING */ { "thinking",     0,    0,               IDLE     },
  /* READING  */ { "reading",      0,    0,               IDLE     },
  /* WRITING  */ { "writing",      0,    0,               IDLE     },
  /* FINISHED */ { "finished",  3000,    3000,            READY    },  // protected
  /* READY    */ { "ready",        0,    60u * 1000,      IDLE     },
  /* WAKING   */ { "waking",    1000,    1000,            THINKING },  // protected
  /* SLEEP    */ { "sleep",        0,    0,               IDLE     },
};

// Linger after PostToolUse before a tool state falls back to thinking.
// (Unused in v1 — reading/writing aren't wired up yet. Kept here so the
// tuning knob lives with its friends.)
static constexpr uint32_t kToolLingerMs = 1000;

// ---- State ----------------------------------------------------------------

static State    sCurrent    = IDLE;
static State    sQueued     = IDLE;   // pending transition if in a min-window
static bool     sHasQueued  = false;
static uint32_t sEnteredMs  = 0;

// ---- Internals ------------------------------------------------------------

static void transitionTo(State target) {
  if (target == sCurrent) {
    sHasQueued = false;
    return;
  }
  LOG_INFO("personality: %s -> %s (after %lums)",
           kStates[sCurrent].name, kStates[target].name,
           (unsigned long)(millis() - sEnteredMs));
  sCurrent   = target;
  sEnteredMs = millis();
  sHasQueued = false;
}

// Map a tool name to the state that should represent it. Unmapped tools
// (Bash, Task, MCP tools) fall through to THINKING — we don't want to
// over-fragment the state set.
static State toolToState(const char* name) {
  if (!name || !*name) return THINKING;
  if (strcmp(name, "Read")      == 0 ||
      strcmp(name, "Grep")      == 0 ||
      strcmp(name, "Glob")      == 0 ||
      strcmp(name, "WebFetch")  == 0 ||
      strcmp(name, "WebSearch") == 0) return READING;
  if (strcmp(name, "Write")        == 0 ||
      strcmp(name, "Edit")         == 0 ||
      strcmp(name, "MultiEdit")    == 0 ||
      strcmp(name, "NotebookEdit") == 0) return WRITING;
  return THINKING;
}

// Route incoming activity. Pops SLEEP through WAKING first so the user
// sees a 1s "oh! they're back" beat before any work state.
static void routeToActive(State target) {
  if (sCurrent == SLEEP) request(WAKING);
  else                   request(target);
}

// ---- Hook handler ---------------------------------------------------------

static void onHook(const ClaudeEvents::HookEvent& e) {
  LOG_EVT("hook %s tool=%s", e.hook_type, e.tool_name);

  if (strcmp(e.hook_type, "UserPromptSubmit") == 0) {
    routeToActive(THINKING);
  } else if (strcmp(e.hook_type, "Stop") == 0) {
    // v1: drop straight back to idle. Later this will become FINISHED
    // and drive the celebration animation.
    request(IDLE);
  }
  // PreToolUse / PostToolUse / Notification: not wired in v1.
}

// ---- Public API -----------------------------------------------------------

void begin() {
  sCurrent   = IDLE;
  sQueued    = IDLE;
  sHasQueued = false;
  sEnteredMs = millis();
  ClaudeEvents::onHook(onHook);
  LOG_INFO("personality: start state=%s", kStates[sCurrent].name);
}

void tick() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - sEnteredMs;
  const StateConfig& cfg = kStates[sCurrent];

  // A queued pre-empt becomes eligible once min_ms has passed.
  if (sHasQueued && elapsed >= cfg.min_ms) {
    transitionTo(sQueued);
    return;
  }

  // max_ms timeout: decay to configured next state.
  if (cfg.max_ms > 0 && elapsed >= cfg.max_ms) {
    transitionTo(cfg.on_timeout);
  }
}

void request(State target) {
  if (target == sCurrent) return;
  const uint32_t elapsed = millis() - sEnteredMs;
  if (elapsed < kStates[sCurrent].min_ms) {
    // In a protected window — queue the transition. Last write wins if
    // multiple things request during the window.
    sQueued    = target;
    sHasQueued = true;
    return;
  }
  transitionTo(target);
}

State       current()        { return sCurrent; }
uint32_t    enteredAtMs()    { return sEnteredMs; }
uint32_t    timeInStateMs()  { const uint32_t n = millis();
                                return n >= sEnteredMs ? n - sEnteredMs : 0; }
const char* stateName(State s) {
  return (s < kStateCount) ? kStates[s].name : "?";
}

}  // namespace Personality
