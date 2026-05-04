#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

namespace Face {

void renderTextScene(TFT_eSprite& s, const SceneRenderState& renderState, const SceneContext& ctx,
                     uint32_t now);

}  // namespace Face
