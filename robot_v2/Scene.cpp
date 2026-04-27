#include "Scene.h"

#include "ActivityDots.h"
#include "EffectsRenderer.h"
#include "FaceRenderer.h"
#include "MoodRingRenderer.h"

namespace Face {

void renderScene(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx,
                 int16_t gdy, const SceneRenderState& renderState,
                 const AgentEvents::AgentState& agentState, uint32_t now) {
  s.fillSprite(kBg);

  drawFace(s, p, blinkAmt, gdx, gdy, renderState.state);
  drawEffects(s, now, renderState.read_stream_alpha, renderState.write_stream_alpha);

  if (moodRingEnabledFor(renderState.state)) {
    drawMoodRing(s, (uint8_t)renderState.mood_r, (uint8_t)renderState.mood_g,
                 (uint8_t)renderState.mood_b);
  }

  drawActivityDots(s, renderState, agentState, now);
}

}  // namespace Face
