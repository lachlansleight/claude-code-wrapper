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

void begin();
void tick();

void setVerb(Verb v);
void clearVerb();
void armLinger(uint32_t ms);
void fireOverlay(Verb overlayVerb, uint32_t durationMs);

Verb current();
Verb effective();
bool overlayActive();
uint32_t enteredAtMs();
uint32_t timeInCurrentMs();

const char* verbName(Verb v);
bool parseVerb(const char* text, Verb* outVerb);

}  // namespace VerbSystem
