#pragma once

// Fires attention-getting motion whenever Claude becomes idle (i.e. finishes
// its turn and is waiting on the user). Schedule:
//
//   t = 0     — immediate nudge
//   t = 60s   — second nudge in case the first was missed
//   t = 6min, 11min, ..., 31min — every 5 minutes through the 30-min window
//
// After the window, no further waggles until Claude works again. Starting a
// new turn resets the schedule.

namespace AttractScheduler {

void begin();
void tick();

}  // namespace AttractScheduler
