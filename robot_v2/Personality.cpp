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
  /* FINISHED */ { "finished",  1500,    1500,            EXCITED  },  // protected
  /* EXCITED  */ { "excited",      0,    10u * 1000,      READY    },
  /* READY    */ { "ready",        0,    60u * 1000,      IDLE     },
  /* WAKING   */ { "waking",    1000,    1000,            THINKING },  // protected
  /* SLEEP    */ { "sleep",        0,    0,               IDLE     },
};

// How long to linger in a tool state after PostToolUse before falling back
// to THINKING. Refreshed by any subsequent matching PreToolUse, so bursts
// of same-type tool calls keep the tool state stable.
static constexpr uint32_t kToolLingerMs = 1000;

// ---- State ----------------------------------------------------------------

static State    sCurrent    = IDLE;
static State    sQueued     = IDLE;
static bool     sHasQueued  = false;
static uint32_t sEnteredMs  = 0;

// 0 = no linger active. Non-zero = millis() deadline after which a tool
// state should auto-fall-back to THINKING. Only meaningful when the
// current state is READING or WRITING.
static uint32_t sToolLingerDeadlineMs = 0;

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
  // Entering any non-tool state clears the linger. Entering a tool state
  // also clears it — PreToolUse will re-arm on PostToolUse.
  sToolLingerDeadlineMs = 0;
}

// Tool-name → state. Unmapped tools (Bash, Task, MCP tools) fall through
// to THINKING — we don't over-fragment the state set.
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
    return;
  }

  if (strcmp(e.hook_type, "PreToolUse") == 0) {
    routeToActive(toolToState(e.tool_name));
    sToolLingerDeadlineMs = 0;  // actively in-tool — not lingering
    return;
  }

  if (strcmp(e.hook_type, "PostToolUse") == 0) {
    // Keep current tool state on screen for a beat. If a matching
    // PreToolUse arrives before the deadline, that re-enter resets the
    // timer. If a different-type tool arrives, that pre-empts (which is
    // what we want — Claude has moved on).
    if (sCurrent == READING || sCurrent == WRITING) {
      sToolLingerDeadlineMs = millis() + kToolLingerMs;
    }
    return;
  }

  if (strcmp(e.hook_type, "Stop") == 0) {
    // "Hooray! I'm done."
    request(FINISHED);
    return;
  }
}

// ---- Public API -----------------------------------------------------------

void begin() {
  sCurrent   = SLEEP;
  sQueued    = SLEEP;
  sHasQueued = false;
  sEnteredMs = millis();
  sToolLingerDeadlineMs = 0;
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

  // Tool-linger fallback: expired → back to THINKING.
  if (sToolLingerDeadlineMs != 0 && now >= sToolLingerDeadlineMs &&
      (sCurrent == READING || sCurrent == WRITING)) {
    sToolLingerDeadlineMs = 0;
    transitionTo(THINKING);
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
    // In a protected window — queue. Last write wins.
    sQueued    = target;
    sHasQueued = true;
    return;
  }
  transitionTo(target);
}

State       current()       { return sCurrent; }
uint32_t    enteredAtMs()   { return sEnteredMs; }
uint32_t    timeInStateMs() { const uint32_t n = millis();
                              return n >= sEnteredMs ? n - sEnteredMs : 0; }
const char* stateName(State s) {
  return (s < kStateCount) ? kStates[s].name : "?";
}

}  // namespace Personality
