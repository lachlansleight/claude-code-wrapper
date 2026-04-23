#include "AttractScheduler.h"

#include <Arduino.h>

#include "ClaudeEvents.h"
#include "DebugLog.h"
#include "Motion.h"

namespace AttractScheduler {

// Offsets (ms) from idle entry. Must be strictly ascending.
static const uint32_t kSchedule[] = {
  0UL,
  60UL  * 1000
};
static const uint8_t kScheduleLen =
    sizeof(kSchedule) / sizeof(kSchedule[0]);

// Debounce window before we treat a working→idle transition as a real idle
// entry. Guards against mid-response Stop flaps, brief gaps between tool
// calls, and similar transients. Any working=true hit during this window
// bounces us straight back to Working without ever firing a waggle.
static constexpr uint32_t kIdleGraceMs = 3000;

// Boot → Working avoids firing on the power-on idle window, before Claude
// has done anything. IdleGrace is the debounce before Idle proper.
enum class Phase : uint8_t { Boot, Working, IdleGrace, Idle };

static Phase    phase         = Phase::Boot;
static uint32_t graceStart    = 0;
static uint32_t idleStart     = 0;
static uint8_t  nextIdx       = 0;

void begin() {
  phase   = Phase::Boot;
  nextIdx = 0;
}

void tick() {
  const bool working = ClaudeEvents::state().working;

  switch (phase) {
    case Phase::Boot:
      if (working) phase = Phase::Working;
      return;

    case Phase::Working:
      if (!working) {
        phase      = Phase::IdleGrace;
        graceStart = millis();
      }
      return;

    case Phase::IdleGrace:
      if (working) {
        phase = Phase::Working;
        return;
      }
      if (millis() - graceStart >= kIdleGraceMs) {
        phase     = Phase::Idle;
        idleStart = millis();
        nextIdx   = 0;
        LOG_EVT("attract: idle confirmed");
      }
      return;

    case Phase::Idle:
      if (working) {
        phase = Phase::Working;
        LOG_EVT("attract: working resumed, schedule reset");
        return;
      }
      while (nextIdx < kScheduleLen &&
             (millis() - idleStart) >= kSchedule[nextIdx]) {
        LOG_EVT("attract: waggle #%u at +%lums",
                nextIdx, (unsigned long)kSchedule[nextIdx]);
        Motion::playWaggle();
        nextIdx++;
      }
      return;
  }
}

}  // namespace AttractScheduler
