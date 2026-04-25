#include "MotionBehaviors.h"

#include "DebugLog.h"
#include "Motion.h"
#include "Personality.h"

namespace MotionBehaviors {

// ---- Tunables -------------------------------------------------------------

// Idle: occasional drift to a new resting position in the [-30, -20] range.
// Reads as "alive but not demanding attention" — small, infrequent moves
// off to one side rather than waggles around centre.
static constexpr uint32_t kIdleDriftMinMs    = 5000;
static constexpr uint32_t kIdleDriftMaxMs    = 10000;
static constexpr int8_t   kIdleDriftLo       = -30;
static constexpr int8_t   kIdleDriftHi       = -20;
static constexpr uint16_t kIdleDriftSlewMs   = 500;   // slow, calm slew

// Excited: continuous oscillation between two angles, ~1 s period.
// Each half-swing slews over kExcitedOscHalfPeriodMs so motion is smooth
// and continuous (no pauses at the endpoints).
static constexpr int8_t   kExcitedOscLo           = -20;
static constexpr int8_t   kExcitedOscHi           = -10;
static constexpr uint16_t kExcitedOscHalfPeriodMs = 500;

// Ready: small slow drift around centre, similar in feel to idle's drift
// but centred and slightly more frequent.
static constexpr uint32_t kReadyDriftMinMs  = 2000;
static constexpr uint32_t kReadyDriftMaxMs  = 3000;
static constexpr int8_t   kReadyDriftLo     = -8;
static constexpr int8_t   kReadyDriftHi     =  8;
static constexpr uint16_t kReadyDriftSlewMs = 500;

// Writing: rhythmic micro-jogs alternating sides, like head-nodding.
static constexpr uint32_t kWritingJogMs  = 420;
static constexpr int8_t   kWritingJogMag = 4;

// Finished: a short burst of waggles over the 3s protected window.
static constexpr uint32_t kFinishedWagglePeriodMs = 900;

// Waking startle: one big immediate jog on entry.
static constexpr int8_t kWakingStartleMag = 18;

// ---- State tracking ------------------------------------------------------

static Personality::State sLastState = Personality::kStateCount;
static uint32_t sStateEntryMs  = 0;
static uint32_t sNextTimedMs   = 0;  // next scheduled event (idle waggle, writing jog, etc.)
static int8_t   sWriteToggle   = 1;
static bool     sExcitedAtLow  = false;  // tracks current end of the excited oscillation

// ---- Helpers (idle) ------------------------------------------------------

static int8_t randIdleOffset() {
  // Arduino random(lo, hi) returns lo..(hi-1).
  return (int8_t)random((long)kIdleDriftLo, (long)kIdleDriftHi + 1);
}

static int8_t randReadyOffset() {
  return (int8_t)random((long)kReadyDriftLo, (long)kReadyDriftHi + 1);
}

// ---- Helpers -------------------------------------------------------------

static uint32_t randRange(uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;
  return lo + (uint32_t)random((long)(hi - lo + 1));
}

// Called once each time we *enter* a state. Sets up any entry action and
// schedules the first timed event for this state.
static void onEnter(Personality::State s) {
  const uint32_t now = millis();
  sStateEntryMs = now;
  sWriteToggle  = (int8_t)(random(2) ? 1 : -1);

  // Thinking mode on the servo is specific to the THINKING state only —
  // other states either sit still or have their own rhythm.
  Motion::setThinkingMode(s == Personality::THINKING);

  switch (s) {
    case Personality::IDLE:
      // Move to a starting resting pose immediately so we don't sit at
      // the previous state's pose; then schedule the next drift.
      Motion::playJog(randIdleOffset(), kIdleDriftSlewMs);
      sNextTimedMs = now + randRange(kIdleDriftMinMs, kIdleDriftMaxMs);
      break;

    case Personality::THINKING:
      // Motion::setThinkingMode already running; nothing else to schedule.
      sNextTimedMs = 0;
      break;

    case Personality::READING:
      // A single brief jog on entry, then stillness.
      Motion::playJog(-8);
      sNextTimedMs = 0;
      break;

    case Personality::WRITING:
      // Rhythmic jogs — first one right away, then every kWritingJogMs.
      Motion::playJog((int8_t)(kWritingJogMag * sWriteToggle));
      sWriteToggle = (int8_t)-sWriteToggle;
      sNextTimedMs = now + kWritingJogMs;
      break;

    case Personality::FINISHED:
      // "Hooray!" — big waggle to kick it off; keep waggling over the
      // 3s protected window.
      Motion::playWaggle();
      sNextTimedMs = now + kFinishedWagglePeriodMs;
      break;

    case Personality::EXCITED:
      // Continuous oscillation — kick off heading to the low end, alternate
      // every kExcitedOscHalfPeriodMs.
      sExcitedAtLow = true;
      Motion::playJog(kExcitedOscLo, kExcitedOscHalfPeriodMs);
      sNextTimedMs = now + kExcitedOscHalfPeriodMs;
      break;

    case Personality::READY:
      // First small drift on entry — also serves as the slew from
      // wherever excited left the arms.
      Motion::playJog(randReadyOffset(), kReadyDriftSlewMs);
      sNextTimedMs = now + randRange(kReadyDriftMinMs, kReadyDriftMaxMs);
      break;

    case Personality::WAKING:
      // Startle on entry.
      Motion::playJog(kWakingStartleMag);
      sNextTimedMs = 0;
      break;

    case Personality::SLEEP:
      // Settle to centre and hold.
      Motion::playJog(0);
      sNextTimedMs = 0;
      break;

    default:
      sNextTimedMs = 0;
      break;
  }
}

// Called every tick while a given state is current. Fires scheduled
// repeating behaviours (idle waggles, writing jogs, finished waggle train).
static void onDuring(Personality::State s) {
  if (sNextTimedMs == 0) return;
  const uint32_t now = millis();
  if (now < sNextTimedMs) return;

  switch (s) {
    case Personality::IDLE:
      Motion::playJog(randIdleOffset(), kIdleDriftSlewMs);
      sNextTimedMs = now + randRange(kIdleDriftMinMs, kIdleDriftMaxMs);
      break;

    case Personality::EXCITED:
      sExcitedAtLow = !sExcitedAtLow;
      Motion::playJog(sExcitedAtLow ? kExcitedOscLo : kExcitedOscHi,
                      kExcitedOscHalfPeriodMs);
      sNextTimedMs = now + kExcitedOscHalfPeriodMs;
      break;

    case Personality::READY:
      Motion::playJog(randReadyOffset(), kReadyDriftSlewMs);
      sNextTimedMs = now + randRange(kReadyDriftMinMs, kReadyDriftMaxMs);
      break;

    case Personality::WRITING:
      Motion::playJog((int8_t)(kWritingJogMag * sWriteToggle));
      sWriteToggle = (int8_t)-sWriteToggle;
      sNextTimedMs = now + kWritingJogMs;
      break;

    case Personality::FINISHED:
      Motion::playWaggle();
      sNextTimedMs = now + kFinishedWagglePeriodMs;
      break;

    default:
      sNextTimedMs = 0;
      break;
  }
}

// ---- Public --------------------------------------------------------------

void begin() {
  sLastState    = Personality::kStateCount;
  sStateEntryMs = 0;
  sNextTimedMs  = 0;
  sWriteToggle  = 1;
}

void tick() {
  const Personality::State s = Personality::current();
  if (s != sLastState) {
    LOG_EVT("motion: enter %s", Personality::stateName(s));
    sLastState = s;
    onEnter(s);
  }
  onDuring(s);
}

}  // namespace MotionBehaviors
