#include "Personality.h"

#include <string.h>

#include "AgentEvents.h"
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
  /* EXECUTING */ { "executing",   0,    5u * 1000,       EXECUTING_LONG },
  /* EXEC_LONG */ { "executingLong", 0,  30u * 1000,      BLOCKED  },
  /* FINISHED */ { "finished",  1500,    1500,            EXCITED  },  // protected
  /* EXCITED  */ { "excited",      0,    10u * 1000,      READY    },
  /* READY    */ { "ready",        0,    60u * 1000,      IDLE     },
  /* WAKING   */ { "waking",    1000,    1000,            THINKING },  // protected
  /* SLEEP    */ { "sleep",        0,    0,               IDLE     },
  /* BLOCKED  */ { "blocked",      0,    0,               IDLE     },  // held while pending_permission set
};

// State to return to when a pending permission resolves. Captured at the
// moment we transition into BLOCKED so we can restore prior context.
static State sPreBlockedState = THINKING;

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

// Where to land after the WAKING beat. Defaults to THINKING (the original
// behaviour). SessionStart-from-SLEEP overrides this to READY.
static State sPostWakeTarget = THINKING;
// True when BLOCKED was entered because a permission request is pending.
// In that case we auto-resume once permission clears; timeout-driven BLOCKED
// should persist until another explicit event arrives.
static bool sBlockedByPermission = false;

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
  if (target != BLOCKED) sBlockedByPermission = false;
}

// Tool-name → state via read/write capability classification.
// Write-capable tools map to WRITING; everything else maps to READING.
static State activityToState(const AgentEvents::Event& e) {
  if (e.activity_kind && !strcmp(e.activity_kind, "shell.exec")) {
    return EXECUTING;
  }
  if (AgentEvents::classifyActivity(e.activity_kind, e.activity_tool, e.activity_summary) ==
      AgentEvents::ACTIVITY_WRITE) {
    return WRITING;
  }
  return READING;
}

// Route incoming activity. Pops SLEEP through WAKING first so the user
// sees a 1s "oh! they're back" beat before any work state.
static void routeToActive(State target) {
  if (sCurrent == SLEEP) {
    sPostWakeTarget = target;
    request(WAKING);
  } else {
    request(target);
  }
}

// ---- Event handler --------------------------------------------------------

static void onAgentEvent(const AgentEvents::Event& e) {
  LOG_EVT("agent-event %s tool=%s", e.kind, e.activity_tool);

  if (strcmp(e.kind, "session.started") == 0) {
    // Greet the user based on what state we were already in:
    //   SLEEP → wake up and land in READY (calmer "back at it" beat)
    //   IDLE  → perk up to READY
    //   READY → already up; a fresh session is exciting
    if (sCurrent == SLEEP)      routeToActive(READY);
    else if (sCurrent == IDLE)  request(READY);
    else if (sCurrent == READY) request(EXCITED);
    return;
  }

  if (strcmp(e.kind, "turn.started") == 0) {
    routeToActive(THINKING);
    return;
  }

  if (strcmp(e.kind, "activity.started") == 0) {
    routeToActive(activityToState(e));
    sToolLingerDeadlineMs = 0;  // actively in-tool — not lingering
    return;
  }

  if (strcmp(e.kind, "activity.finished") == 0 ||
      strcmp(e.kind, "activity.failed") == 0) {
    // Keep current tool state on screen for a beat. If a matching
    // PreToolUse arrives before the deadline, that re-enter resets the
    // timer. If a different-type tool arrives, that pre-empts (which is
    // what we want — Claude has moved on).
    if (sCurrent == READING || sCurrent == WRITING || sCurrent == EXECUTING ||
        sCurrent == EXECUTING_LONG) {
      sToolLingerDeadlineMs = millis() + kToolLingerMs;
    }
    return;
  }

  if (strcmp(e.kind, "turn.ended") == 0) {
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
  sBlockedByPermission = false;
  AgentEvents::onEvent(onAgentEvent);
  LOG_INFO("personality: start state=%s", kStates[sCurrent].name);
}

void tick() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - sEnteredMs;
  const StateConfig& cfg = kStates[sCurrent];

  // Permission gating, polled (matches the AmbientMotion pattern — the
  // AgentEvents permission callback is single-slot and already taken by
  // robot_v2.ino, so we observe state instead of stealing it).
  const bool pending = AgentEvents::state().pending_permission[0] != '\0';
  if (pending && sCurrent != BLOCKED && sCurrent != WAKING) {
    sPreBlockedState = sCurrent;
    sBlockedByPermission = true;
    transitionTo(BLOCKED);
    return;
  }
  if (!pending && sCurrent == BLOCKED && sBlockedByPermission) {
    // Verdict landed — back to whatever we were doing. Claude will
    // typically follow up with a hook event that overrides this anyway.
    sBlockedByPermission = false;
    transitionTo(sPreBlockedState);
    return;
  }

  // A queued pre-empt becomes eligible once min_ms has passed.
  if (sHasQueued && elapsed >= cfg.min_ms) {
    transitionTo(sQueued);
    return;
  }

  // Tool-linger fallback: expired → back to THINKING.
  if (sToolLingerDeadlineMs != 0 && now >= sToolLingerDeadlineMs &&
      (sCurrent == READING || sCurrent == WRITING ||
       sCurrent == EXECUTING || sCurrent == EXECUTING_LONG)) {
    sToolLingerDeadlineMs = 0;
    transitionTo(THINKING);
    return;
  }

  // max_ms timeout: decay to configured next state. WAKING uses the
  // post-wake target set by routeToActive() instead of its static
  // on_timeout, so the entry point decides where waking lands.
  if (cfg.max_ms > 0 && elapsed >= cfg.max_ms) {
    State next = (sCurrent == WAKING) ? sPostWakeTarget : cfg.on_timeout;
    transitionTo(next);
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
