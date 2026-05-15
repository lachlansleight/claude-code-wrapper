#include "VerbSystem.h"

#include <ctype.h>
#include <string.h>

#include "../face/FACE_CONFIG.h"
#include "../face/VerbTimeline.h"

namespace VerbSystem {

namespace {

Verb sCurrent = Verb::None;
uint32_t sEnteredAtMs = 0;
uint32_t sLingerUntilMs = 0;

bool sTransientActive = false;
Verb sTransientVerb = Verb::None;
Verb sTransientPostVerb = Verb::None;
uint32_t sTransientUntilMs = 0;
uint32_t sTransientBlendOutAtMs = 0;
uint32_t sTransientStartedMs = 0;

bool ieq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

bool canPlayTransient(Verb v) { return v != Verb::None; }

void applyPostVerb(Verb post) {
  if (post == Verb::None) {
    sCurrent = Verb::None;
  } else {
    sCurrent = post;
  }
  sEnteredAtMs = millis();
  sLingerUntilMs = 0;
}

void startTransient(Verb overlayVerb, uint32_t durationMs, bool explicitPost, Verb postOverlayVerb) {
  if (!canPlayTransient(overlayVerb)) return;
  if (durationMs == 0) durationMs = 1;

  const uint32_t now = millis();
  const uint32_t blendMs = Face::kVerbTransitionDurMs;
  if (durationMs <= blendMs) durationMs = blendMs + 1;

  sTransientPostVerb = explicitPost ? postOverlayVerb : sCurrent;

  sTransientActive = true;
  sTransientVerb = overlayVerb;
  sTransientStartedMs = now;
  sTransientUntilMs = now + durationMs;
  sTransientBlendOutAtMs = now + durationMs - blendMs;
}

void endTransient() {
  const Verb post = sTransientPostVerb;
  sTransientActive = false;
  sTransientVerb = Verb::None;
  applyPostVerb(post);
}

}  // namespace

void begin() {
  sCurrent = Verb::Sleeping;
  sEnteredAtMs = millis();
  sLingerUntilMs = 0;
  sTransientActive = false;
  sTransientVerb = Verb::None;
  sTransientPostVerb = Verb::None;
  sTransientUntilMs = 0;
  sTransientBlendOutAtMs = 0;
  sTransientStartedMs = 0;
}

void tick() {
  const uint32_t now = millis();

  if (sTransientActive && now >= sTransientUntilMs) {
    endTransient();
    return;
  }

  if (sLingerUntilMs != 0 && now >= sLingerUntilMs) {
    sLingerUntilMs = 0;
    if (sCurrent != Verb::None && sCurrent != Verb::Sleeping) {
      sCurrent = Verb::Thinking;
      sEnteredAtMs = now;
    }
  }

  if (sCurrent == Verb::Executing &&
      (now - sEnteredAtMs) >= FaceConfig::kVerbSim.strain_delay_ms) {
    sCurrent = Verb::Straining;
    sEnteredAtMs = now;
  }
}

void setVerb(Verb v) {
  if (v == Verb::None) {
    clearVerb();
    return;
  }
  sTransientActive = false;
  sCurrent = v;
  sEnteredAtMs = millis();
  sLingerUntilMs = 0;
}

void clearVerb() {
  sTransientActive = false;
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
  startTransient(overlayVerb, durationMs, false, Verb::None);
}

void fireOverlay(Verb overlayVerb, uint32_t durationMs, Verb postOverlayVerb) {
  startTransient(overlayVerb, durationMs, true, postOverlayVerb);
}

Verb current() { return sCurrent; }

Verb effective() {
  if (!sTransientActive) return sCurrent;
  const uint32_t now = millis();
  if (now >= sTransientBlendOutAtMs) return sTransientPostVerb;
  return sTransientVerb;
}

bool overlayActive() { return sTransientActive; }

uint32_t enteredAtMs() { return sEnteredAtMs; }

uint32_t timeInCurrentMs() { return millis() - sEnteredAtMs; }

uint32_t timeInEffectiveMs() {
  if (sTransientActive) return millis() - sTransientStartedMs;
  return timeInCurrentMs();
}

uint32_t effectiveEnteredAtMs() {
  if (sTransientActive) return sTransientStartedMs;
  return sEnteredAtMs;
}

DebugState debugState() {
  const Verb eff = effective();
  return DebugState{
      sCurrent,
      eff,
      sTransientActive ? sTransientVerb : Verb::None,
      sTransientPostVerb,
      Verb::None,
      sTransientActive,
      false,
      sEnteredAtMs,
      sLingerUntilMs,
      sTransientUntilMs,
      0,
  };
}

const char* verbName(Verb v) {
  const uint8_t i = (uint8_t)v;
  if (i >= (uint8_t)Verb::Count) return "?";
  return FaceConfig::kVerbSlugs[i];
}

bool parseVerb(const char* text, Verb* outVerb) {
  if (!text || !outVerb) return false;
  if (ieq(text, "attractingattention")) {
    text = "attracting_attention";
  }
  for (uint8_t i = 0; i < (uint8_t)Verb::Count; ++i) {
    if (ieq(text, FaceConfig::kVerbSlugs[i])) {
      *outVerb = (Verb)i;
      return true;
    }
  }
  return false;
}

}  // namespace VerbSystem
