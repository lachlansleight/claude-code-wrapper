#include "MotionBehaviors.h"

#include "DebugLog.h"
#include "Motion.h"
#include "Personality.h"

namespace MotionBehaviors {

// =============================================================================
// TUNING — every motion knob in the firmware lives in this file. To change
// how a state moves, edit the row in `kMotion` for that state and reflash;
// nothing outside this file needs to know.
// =============================================================================

// ---- Safe servo range ------------------------------------------------------
//
// Hard physical limits in signed degrees relative to mechanical centre
// (0 = centre, +90 / -90 = extremes). Anything outside this range can
// damage the chassis. `Motion` clamps every output to this range, so a
// typo in the table below can't drive into something solid.
static constexpr int8_t kSafeMin = -25;
static constexpr int8_t kSafeMax =  25;

// ---- Default slew durations ------------------------------------------------
//
// Used when a table entry leaves `slewMs == 0`. These exist so a row can
// say "default is fine" without needing to specify a magic number.
static constexpr uint16_t kDefaultStaticSlewMs = 250;
static constexpr uint16_t kDefaultDriftSlewMs  = 500;

// ---- Motion modes ----------------------------------------------------------
//
// NONE          — no motion; servo holds wherever the previous state left it.
//                 Default for unconfigured states; cancels thinking-mode and
//                 any in-flight pattern on entry.
//
// STATIC        — slew once to `center` on entry, then still. `slewMs` is
//                 the entry slew (0 = kDefaultStaticSlewMs).
//
// RANDOM_DRIFT  — pick a random offset in [center-amplitude, center+amplitude],
//                 slew to it over `slewMs`, hold for
//                 random(periodMs, periodMs+periodJitterMs), then pick a new
//                 target. Reads as "alive but unhurried". `slewMs` 0 →
//                 kDefaultDriftSlewMs.
//
// OSCILLATE     — two-point ping-pong between (center-amplitude) and
//                 (center+amplitude). `periodMs` is the FULL cycle (each leg
//                 is periodMs/2). `slewMs` is per-leg slew: set it equal to
//                 periodMs/2 for continuous motion, or smaller for a snappy
//                 "jog and hold" feel. `slewMs` 0 → periodMs/2 (continuous).
//
// WAGGLE        — 5-frame waggle (centre → +amp → -amp → +amp → centre).
//                 The waggle itself takes ~periodMs/2 and is retriggered
//                 every periodMs while the state is active (equal pause
//                 between waggles). `slewMs` unused.
//
// THINKING      — smooth sine oscillation around `center` with ±`amplitude`
//                 over a full period of `periodMs`. Eases in over the first
//                 second on entry. `slewMs` unused.
enum MotionMode : uint8_t {
  NONE = 0,
  STATIC,
  RANDOM_DRIFT,
  OSCILLATE,
  WAGGLE,
  THINKING,
};

struct StateMotion {
  MotionMode mode;
  int8_t   center;
  uint8_t  amplitude;
  uint16_t periodMs;
  uint16_t periodJitterMs;
  uint16_t slewMs;
};

// ---- Per-state table -------------------------------------------------------
//
// Rows are indexed by Personality::State, in enum order. KEEP THIS ORDER
// IN SYNC with the enum in Personality.h — the static_assert below will
// catch a missing/extra row at compile time. The labels in comments are
// just for navigation; only position matters.
//
// Field order: { mode, center, amplitude, periodMs, periodJitterMs, slewMs }
static const StateMotion kMotion[Personality::kStateCount] = {
  // IDLE — alive but not demanding attention. Slow drifts off to one
  //   side, every 5–10 s. center=-20, amp=5 → range [-25, -15].
  /* IDLE            */ { RANDOM_DRIFT, -20, 5,  5000, 5000, 500 },

  // THINKING — gentle sine wave around centre while the agent is mid-thought.
  /* THINKING        */ { THINKING,       -15, 5,  2000,    0,   0 },

  // READING — single small lean on entry, then still.
  /* READING         */ { STATIC,        -8, 0,     0,    0,   0 },

  // WRITING — twitchy alternating jogs around centre. Short period +
  //   short slew gives a head-nod-while-typing feel.
  /* WRITING         */ { OSCILLATE,      5, 4,   840,    0, 250 },

  // EXECUTING — calm continuous oscillation while a tool runs. Slew per
  //   leg = half-period (slewMs=0) so the arm never pauses.
  /* EXECUTING       */ { OSCILLATE,    -5, 5,  1000,    0,   0 },

  // EXECUTING_LONG — same shape as EXECUTING but faster — "still working,
  //   getting a bit antsy".
  /* EXECUTING_LONG  */ { OSCILLATE,    0, 5,   750,    0,   0 },

  // FINISHED — celebratory waggle every 900 ms (≈450 ms shake, 450 ms pause).
  /* FINISHED        */ { WAGGLE,         0, 15,  900,    0,   0 },

  // EXCITED — same oscillation as EXECUTING for now (post-finished energy).
  /* EXCITED         */ { OSCILLATE,    -10, 5,  1000,    0,   0 },

  // READY — calmer drift around centre, faster cadence than IDLE.
  /* READY           */ { RANDOM_DRIFT,   -15, 8,  2000, 1000, 500 },

  // WAKING — startle: snap toward one extreme.
  /* WAKING          */ { STATIC,        18, 0,     0,    0,   0 },

  // SLEEP — settle to centre and hold.
  /* SLEEP          */ { OSCILLATE,        -20, 5,     8000,    0,   0 },

  // BLOCKED — awaiting permission verdict; intentionally still for now.
  /* BLOCKED         */ { NONE,           0, 0,     0,    0,   0 },

  // WANTS_ATTENTION — short waggle burst to flag the user.
  /* WANTS_ATTENTION */ { WAGGLE,         0, 15,  900,    0,   0 },
};

static_assert(sizeof(kMotion) / sizeof(kMotion[0]) == Personality::kStateCount,
              "kMotion table size must match Personality::kStateCount — "
              "did you add a state without adding a row?");

// =============================================================================
// Runtime — driven by Personality::current() each tick. Reads the table
// above; this section is the dispatch glue, not configuration.
// =============================================================================

static Personality::State sLastState   = Personality::kStateCount;
static uint32_t           sNextTimedMs = 0;     // ms when the next scheduled event fires

// OSCILLATE: which end we're heading to next.
static bool sOscAtLow = false;

static int8_t randInRange(int8_t lo, int8_t hi) {
  if (lo > hi) { const int8_t t = lo; lo = hi; hi = t; }
  return (int8_t)random((long)lo, (long)hi + 1);
}

static uint32_t randRange(uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;
  return lo + (uint32_t)random((long)(hi - lo + 1));
}

static int8_t driftPick(const StateMotion& m) {
  return randInRange((int8_t)((int)m.center - (int)m.amplitude),
                     (int8_t)((int)m.center + (int)m.amplitude));
}

// Schedule the first event of a state and apply its entry action.
static void onEnter(Personality::State s) {
  const StateMotion& m = kMotion[s];
  const uint32_t now = millis();

  // Thinking mode is special: only THINKING leaves it on. Every other
  // state turns it off so the sine wave doesn't bleed across.
  if (m.mode != THINKING) Motion::setThinkingMode(false);

  switch (m.mode) {
    case NONE:
      Motion::cancelAll();
      sNextTimedMs = 0;
      break;

    case STATIC:
      Motion::playJog(m.center, m.slewMs ? m.slewMs : kDefaultStaticSlewMs);
      sNextTimedMs = 0;
      break;

    case RANDOM_DRIFT: {
      Motion::playJog(driftPick(m), m.slewMs ? m.slewMs : kDefaultDriftSlewMs);
      sNextTimedMs = now + randRange(m.periodMs,
                                     (uint32_t)m.periodMs + m.periodJitterMs);
      break;
    }

    case OSCILLATE: {
      sOscAtLow = true;
      const uint16_t halfMs = (uint16_t)(m.periodMs / 2);
      const uint16_t slew   = m.slewMs ? m.slewMs : halfMs;
      Motion::playJog((int8_t)((int)m.center - (int)m.amplitude), slew);
      sNextTimedMs = now + halfMs;
      break;
    }

    case WAGGLE:
      Motion::playWaggle(m.center, m.amplitude, m.periodMs);
      sNextTimedMs = now + m.periodMs;
      break;

    case THINKING:
      Motion::setThinkingMode(true, m.center, m.amplitude, m.periodMs);
      sNextTimedMs = 0;
      break;
  }
}

// Fire the next scheduled event for the current state, if its time has come.
static void onDuring(Personality::State s) {
  if (sNextTimedMs == 0) return;
  const uint32_t now = millis();
  if (now < sNextTimedMs) return;

  const StateMotion& m = kMotion[s];

  switch (m.mode) {
    case RANDOM_DRIFT:
      Motion::playJog(driftPick(m), m.slewMs ? m.slewMs : kDefaultDriftSlewMs);
      sNextTimedMs = now + randRange(m.periodMs,
                                     (uint32_t)m.periodMs + m.periodJitterMs);
      break;

    case OSCILLATE: {
      sOscAtLow = !sOscAtLow;
      const int8_t off = sOscAtLow
          ? (int8_t)((int)m.center - (int)m.amplitude)
          : (int8_t)((int)m.center + (int)m.amplitude);
      const uint16_t halfMs = (uint16_t)(m.periodMs / 2);
      const uint16_t slew   = m.slewMs ? m.slewMs : halfMs;
      Motion::playJog(off, slew);
      sNextTimedMs = now + halfMs;
      break;
    }

    case WAGGLE:
      Motion::playWaggle(m.center, m.amplitude, m.periodMs);
      sNextTimedMs = now + m.periodMs;
      break;

    case NONE:
    case STATIC:
    case THINKING:
      sNextTimedMs = 0;
      break;
  }
}

// ---- Queries (for face sync) ----------------------------------------------

uint16_t periodMsFor(Personality::State s) {
  if ((unsigned)s >= (unsigned)Personality::kStateCount) return 0;
  const StateMotion& m = kMotion[s];
  switch (m.mode) {
    case OSCILLATE:
    case WAGGLE:
    case THINKING:
      return m.periodMs;
    case NONE:
    case STATIC:
    case RANDOM_DRIFT:    // drift periods are randomized — no stable phase
    default:
      return 0;
  }
}

// ---- Public ----------------------------------------------------------------

void begin() {
  Motion::setSafeRange(kSafeMin, kSafeMax);
  sLastState   = Personality::kStateCount;
  sNextTimedMs = 0;
}

void tick() {
  const Personality::State s = Personality::current();
  if (s != sLastState) {
    LOG_EVT("motion: enter %s", Personality::stateName(s));
    sLastState = s;
    onEnter(s);
  } else if (Motion::consumeHoldExpired()) {
    LOG_EVT("motion: hold expired, re-entering %s", Personality::stateName(s));
    onEnter(s);
  }
  onDuring(s);
}

}  // namespace MotionBehaviors
