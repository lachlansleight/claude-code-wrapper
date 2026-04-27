#pragma once

#include <TFT_eSPI.h>

#include "Personality.h"

namespace Face {

bool moodRingEnabledFor(Personality::State st);
void drawMoodRing(TFT_eSprite& s, uint8_t r, uint8_t g, uint8_t b);

}  // namespace Face
