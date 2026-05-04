#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

namespace Face {

bool moodRingEnabledFor(Expression expr);
void drawMoodRing(TFT_eSprite& s, uint8_t r, uint8_t g, uint8_t b);

}  // namespace Face
