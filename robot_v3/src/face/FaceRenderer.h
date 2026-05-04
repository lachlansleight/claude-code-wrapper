#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

namespace Face {

void drawFace(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx, int16_t gdy,
              Expression expr, uint16_t fg565, uint16_t bg565);

}  // namespace Face
