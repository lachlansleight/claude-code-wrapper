#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

namespace Face {

void renderScene(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx, int16_t gdy,
                  const SceneRenderState& renderState, const SceneContext& ctx, uint32_t now);

}  // namespace Face
