#pragma once

#include <Arduino.h>

namespace Face {

void begin();
void tick();

// Force a redraw on the next tick (e.g. after returning from the portal).
void invalidate();

}  // namespace Face
