#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

/**
 * @file Scene.h
 * @brief Top-level "draw the procedural face scene" entrypoint.
 *
 * Composes the four face-mode renderers in fixed Z-order:
 *   1. background fill,
 *   2. FaceRenderer::drawFace (eyes + mouth, rotated/translated),
 *   3. EffectsRenderer::drawEffects (left/right token streams),
 *   4. MoodRingRenderer::drawMoodRing (only for expressions that opt in),
 *   5. ActivityDots overlay.
 *
 * Called by FrameController once per frame in face mode; text/debug
 * mode goes through TextScene::renderTextScene instead.
 */
namespace Face {

/**
 * Render one full face frame into @p s.
 *
 * @param s             Sprite framebuffer (cleared to the bg colour by this call).
 * @param p             Tweened FaceParams for this frame.
 * @param blinkAmt      Eye blink amount in [0, 1] (0 = open, 1 = fully closed).
 * @param gdx           Gaze X offset added to the pupil position.
 * @param gdy           Gaze Y offset added to the pupil position.
 * @param renderState   Per-frame derived state (mood ring, alphas, palette).
 * @param ctx           Scene snapshot — read for activity-dot counters etc.
 * @param now           millis() for animations.
 */
void renderScene(TFT_eSprite& s, const FaceParams& p, float blinkAmt, int16_t gdx, int16_t gdy,
                  const SceneRenderState& renderState, const SceneContext& ctx, uint32_t now);

}  // namespace Face
