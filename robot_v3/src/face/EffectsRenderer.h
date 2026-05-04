#pragma once

#include <TFT_eSPI.h>

namespace Face {

void drawEffects(TFT_eSprite& s, uint32_t now, float readAlpha, float writeAlpha);

}  // namespace Face
