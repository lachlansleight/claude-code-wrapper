#include "AmbientMotion.h"

#include <Arduino.h>
#include <string.h>

#include "AgentEvents.h"
#include "DebugLog.h"
#include "Motion.h"

namespace AmbientMotion {

// Previous-tick snapshot of the fields we watch. Edge detection compares
// against these, so we can infer PreToolUse / PostToolUse without needing
// the hook callback slot.
static char     prevTool[32]     = "";
static uint32_t prevEndMs        = 0;

// Signed side of the next jog. Flips after every jog so successive tool
// calls alternate left/right.
static int8_t   nextSign         = 1;

static uint32_t lastJogMs        = 0;
static bool     thinkingOn       = false;

// Delay after the most recent jog before thinking oscillation takes over.
static constexpr uint32_t kThinkingGraceMs = 1000;

// Jog magnitude range (degrees). Both ends inclusive.
static constexpr int kJogMin = 5;
static constexpr int kJogMax = 15;

static void fireJog() {
  const long mag = random(kJogMin, kJogMax + 1);
  const int8_t offset = (int8_t)(mag * nextSign);
  Motion::playJog(offset);
  nextSign   = (int8_t)-nextSign;
  lastJogMs  = millis();

  // Any ongoing thinking oscillation yields to the jog; it'll come back
  // after the grace window if we're still working.
  if (thinkingOn) {
    thinkingOn = false;
    Motion::setThinkingMode(false);
  }
  LOG_EVT("ambient: jog offset=%d", (int)offset);
}

void begin() {
  prevTool[0] = '\0';
  prevEndMs   = 0;
  nextSign    = 1;
  lastJogMs   = 0;
  thinkingOn  = false;
}

void tick() {
  const auto& st = AgentEvents::state();

  // PreToolUse edge detection. Fires exactly once when we see a newly-set
  // current_tool in its "running" state (end_ms == 0). Either the first
  // tool of a turn (prevTool empty) or a follow-up after a PostToolUse
  // (prevEndMs nonzero, then reset to 0). The `current_tool_end_ms == 0`
  // gate is critical: without it, the "after a PostToolUse" branch fires
  // every tick while we're idly sitting post-PostToolUse with prevEndMs
  // still stamped — resulting in continuous random jogs.
  const bool toolStarted =
      st.current_tool[0] && st.current_tool_end_ms == 0 &&
      (!prevTool[0] || prevEndMs != 0);

  if (toolStarted) fireJog();

  strncpy(prevTool, st.current_tool, sizeof(prevTool) - 1);
  prevTool[sizeof(prevTool) - 1] = '\0';
  prevEndMs = st.current_tool_end_ms;

  // Thinking-mode arbitration.
  if (st.working) {
    if (!thinkingOn && (millis() - lastJogMs) >= kThinkingGraceMs) {
      thinkingOn = true;
      Motion::setThinkingMode(true);
    }
  } else if (thinkingOn) {
    thinkingOn = false;
    Motion::setThinkingMode(false);
  }
}

}  // namespace AmbientMotion
