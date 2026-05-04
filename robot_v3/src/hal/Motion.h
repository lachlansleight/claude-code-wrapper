#pragma once

#include <Arduino.h>

namespace Motion {

void begin();
void tick();
void setSafeRange(int8_t minOffsetDeg, int8_t maxOffsetDeg);
void playJog(int8_t offsetDeg, uint16_t durationMs = 250);
void playWaggle(int8_t center, uint8_t amplitude, uint16_t periodMs);
void cancelAll();
void setThinkingMode(bool on, int8_t centerOffset = 0, uint8_t amplitude = 5,
                     uint16_t periodMs = 2000);
void holdPosition(int8_t offsetDeg, uint32_t durationMs);
bool consumeHoldExpired();
bool isBusy();

}  // namespace Motion
