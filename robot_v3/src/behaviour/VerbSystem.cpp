#include "VerbSystem.h"

#include <ctype.h>
#include <string.h>

namespace VerbSystem {

namespace {

static constexpr uint32_t kStrainDelayMs = 5000;

Verb sCurrent = Verb::None;
uint32_t sEnteredAtMs = 0;
uint32_t sLingerUntilMs = 0;

bool sOverlayActive = false;
Verb sOverlayVerb = Verb::None;
uint32_t sOverlayUntilMs = 0;
Verb sPreOverlayVerb = Verb::None;

bool sOverlayQueued = false;
Verb sQueuedOverlayVerb = Verb::None;
uint32_t sQueuedOverlayDurationMs = 0;

bool ieq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

bool isOverlayVerb(Verb v) {
  return v == Verb::Waking || v == Verb::AttractingAttention;
}

}  // namespace

void begin() {
  sCurrent = Verb::Sleeping;
  sEnteredAtMs = millis();
  sLingerUntilMs = 0;
  sOverlayActive = false;
  sOverlayVerb = Verb::None;
  sOverlayUntilMs = 0;
  sPreOverlayVerb = Verb::None;
  sOverlayQueued = false;
}

void tick() {
  const uint32_t now = millis();

  if (sOverlayActive && now >= sOverlayUntilMs) {
    sOverlayActive = false;
    sOverlayVerb = Verb::None;
    sCurrent = sPreOverlayVerb;
    sEnteredAtMs = now;
    if (sOverlayQueued) {
      const Verb queued = sQueuedOverlayVerb;
      const uint32_t duration = sQueuedOverlayDurationMs;
      sOverlayQueued = false;
      fireOverlay(queued, duration);
      return;
    }
  }

  if (sLingerUntilMs != 0 && now >= sLingerUntilMs) {
    sLingerUntilMs = 0;
    if (sCurrent != Verb::None && sCurrent != Verb::Sleeping) {
      sCurrent = Verb::Thinking;
      sEnteredAtMs = now;
    }
  }

  if (sCurrent == Verb::Executing && (now - sEnteredAtMs) >= kStrainDelayMs) {
    sCurrent = Verb::Straining;
    sEnteredAtMs = now;
  }
}

void setVerb(Verb v) {
  if (v == Verb::None) {
    clearVerb();
    return;
  }
  if (isOverlayVerb(v)) {
    fireOverlay(v, 1000);
    return;
  }
  sCurrent = v;
  sEnteredAtMs = millis();
  sLingerUntilMs = 0;
}

void clearVerb() {
  sCurrent = Verb::None;
  sEnteredAtMs = millis();
  sLingerUntilMs = 0;
}

void armLinger(uint32_t ms) {
  if (ms == 0) {
    sLingerUntilMs = 0;
    return;
  }
  sLingerUntilMs = millis() + ms;
}

void fireOverlay(Verb overlayVerb, uint32_t durationMs) {
  if (!isOverlayVerb(overlayVerb)) return;
  if (durationMs == 0) durationMs = 1;
  if (sOverlayActive) {
    sOverlayQueued = true;
    sQueuedOverlayVerb = overlayVerb;
    sQueuedOverlayDurationMs = durationMs;
    return;
  }
  sPreOverlayVerb = sCurrent;
  sOverlayActive = true;
  sOverlayVerb = overlayVerb;
  sOverlayUntilMs = millis() + durationMs;
}

Verb current() { return sCurrent; }
Verb effective() { return sOverlayActive ? sOverlayVerb : sCurrent; }
bool overlayActive() { return sOverlayActive; }
uint32_t enteredAtMs() { return sEnteredAtMs; }
uint32_t timeInCurrentMs() { return millis() - sEnteredAtMs; }

const char* verbName(Verb v) {
  switch (v) {
    case Verb::None:
      return "none";
    case Verb::Thinking:
      return "thinking";
    case Verb::Reading:
      return "reading";
    case Verb::Writing:
      return "writing";
    case Verb::Executing:
      return "executing";
    case Verb::Straining:
      return "straining";
    case Verb::Sleeping:
      return "sleeping";
    case Verb::Waking:
      return "waking";
    case Verb::AttractingAttention:
      return "attracting_attention";
    default:
      return "?";
  }
}

bool parseVerb(const char* text, Verb* outVerb) {
  if (!text || !outVerb) return false;
  if (ieq(text, "none")) *outVerb = Verb::None;
  else if (ieq(text, "thinking")) *outVerb = Verb::Thinking;
  else if (ieq(text, "reading")) *outVerb = Verb::Reading;
  else if (ieq(text, "writing")) *outVerb = Verb::Writing;
  else if (ieq(text, "executing")) *outVerb = Verb::Executing;
  else if (ieq(text, "straining")) *outVerb = Verb::Straining;
  else if (ieq(text, "sleeping")) *outVerb = Verb::Sleeping;
  else if (ieq(text, "waking")) *outVerb = Verb::Waking;
  else if (ieq(text, "attracting_attention") || ieq(text, "attractingattention")) {
    *outVerb = Verb::AttractingAttention;
  } else {
    return false;
  }
  return true;
}

}  // namespace VerbSystem
