#pragma once

#include <TFT_eSPI.h>

#include "AgentEvents.h"
#include "SceneTypes.h"

namespace Face {

void renderScene(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx,
                 int16_t gdy, const SceneRenderState& renderState,
                 const AgentEvents::AgentState& agentState, uint32_t now);

}  // namespace Face
