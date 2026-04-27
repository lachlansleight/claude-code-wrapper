#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

namespace Face {

void drawFace(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx,
              int16_t gdy, Personality::State st);

}  // namespace Face
