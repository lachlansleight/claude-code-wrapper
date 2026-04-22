#pragma once

// Fires attention-getting motion when Claude becomes idle (i.e. finishes its
// turn and is waiting on the user). Current schedule lives in
// AttractScheduler.cpp (`kSchedule`). As of writing it's a short two-waggle
// burst — one at idle entry, one again 60 s later — intended to nudge without
// becoming annoying. Extend the array (strictly ascending ms offsets) to add
// more attempts.
//
// Idle entry is debounced by `kIdleGraceMs` so that mid-response Stop flaps
// or brief gaps between tool calls don't trigger a phantom idle.

namespace AttractScheduler {

void begin();
void tick();

}  // namespace AttractScheduler
