#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

/**
 * @file MoodRingRenderer.h
 * @brief Optional thin coloured ring around the face perimeter.
 *
 * Colour comes from FrameController's smoothed tween of `FaceParams::ring_*`
 * (`FaceConfig::kBaseTargets` literals and emotion blend), not from Settings.
 *
 * Ring is drawn whenever resolved `ring_r/g/b` are non-zero (`drawMoodRing`
 * no-ops on black). No per-expression allow-list.
 */
namespace Face {

/** Always true; visibility is determined by the RGB passed to `drawMoodRing`. */
bool moodRingShouldDraw(Expression expr);

/**
 * Draw a 6-pixel-thick ring (radii 110..115 from screen centre) in the
 * given RGB888 colour. No-op if (r,g,b) == (0,0,0). Intended to be
 * called after the face is drawn but before activity dots.
 */
void drawMoodRing(TFT_eSprite& s, uint8_t r, uint8_t g, uint8_t b);

}  // namespace Face
