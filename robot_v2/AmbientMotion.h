#pragma once

// Low-key servo motion during active Claude sessions.
//
// Behavior:
//   - Every PreToolUse triggers a short "jog" to a random offset in
//     [5, 15] degrees. Direction alternates between positive and negative
//     on successive jogs.
//   - When `working` but no jog has fired for 1 second, the servo drops into
//     a slow sinusoidal oscillation (±5°, 2 s period) — the "thinking" idle.
//   - When not working, ambient motion is off and the attract scheduler
//     takes over (waggle on idle entry).
//
// Drives Motion directly; observes AgentEvents::state() via polling so we
// don't burn the single hook-callback slot.

namespace AmbientMotion {

void begin();
void tick();

}  // namespace AmbientMotion
