#pragma once

#include <TFT_eSPI.h>

#include "AgentEvents.h"
#include "SceneTypes.h"

namespace Face {

void renderTextScene(TFT_eSprite& s, const SceneRenderState& renderState,
                     const AgentEvents::AgentState& agentState, uint32_t now);

}  // namespace Face
