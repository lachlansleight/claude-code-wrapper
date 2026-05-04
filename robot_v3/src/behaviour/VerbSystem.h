#pragma once

#include <Arduino.h>

namespace VerbSystem {

enum class Verb : uint8_t {
  None = 0,
  Thinking,
  Reading,
  Writing,
  Executing,
  Straining,
  Sleeping,
  Waking,
  AttractingAttention,
  Count
};

struct DebugState {
  Verb current;
  Verb effective;
  Verb overlayVerb;
  Verb preOverlayVerb;
  Verb queuedOverlayVerb;
  bool overlayActive;
  bool overlayQueued;
  uint32_t enteredAtMs;
  uint32_t lingerUntilMs;
  uint32_t overlayUntilMs;
  uint32_t queuedOverlayDurationMs;
};

void begin();
void tick();

void setVerb(Verb v);
void clearVerb();
void armLinger(uint32_t ms);
void fireOverlay(Verb overlayVerb, uint32_t durationMs);
void fireOverlay(Verb overlayVerb, uint32_t durationMs, Verb postOverlayVerb);

Verb current();
Verb effective();
bool overlayActive();
uint32_t enteredAtMs();
uint32_t timeInCurrentMs();
DebugState debugState();

const char* verbName(Verb v);
bool parseVerb(const char* text, Verb* outVerb);

}  // namespace VerbSystem
