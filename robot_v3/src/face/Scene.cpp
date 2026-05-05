#include "Scene.h"

#include "ActivityDots.h"
#include "EffectsRenderer.h"
#include "FaceRenderer.h"
#include "MoodRingRenderer.h"

namespace Face {

void renderScene(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx, int16_t gdy,
                  const SceneRenderState& renderState, const SceneContext& ctx, uint32_t now) {
  s.fillSprite(renderState.bg565);

  drawFace(s, p, blinkAmt, gdx, gdy, renderState.expression, now, renderState.fg565,
           renderState.bg565);
  drawEffects(s, now, renderState.read_stream_alpha, renderState.write_stream_alpha);

  if (moodRingEnabledFor(renderState.expression)) {
    drawMoodRing(s, (uint8_t)renderState.mood_r, (uint8_t)renderState.mood_g, (uint8_t)renderState.mood_b);
  }

  drawActivityDots(s, renderState, ctx, now);
}

}  // namespace Face
