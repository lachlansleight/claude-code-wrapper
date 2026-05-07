#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

/**
 * @file MoodRingRenderer.h
 * @brief Optional thin coloured ring around the face perimeter.
 *
 * Colour comes from FrameController's smoothed tween of `FaceParams::ring_*`
 * (kBaseTargets literals and emotion blend), not from Settings.
 *
 * **Emotion** expressions (Neutral … Disappointed) always run the draw
 * path (`drawMoodRing` no-ops when RGB is black). **Verb / overlay**
 * expressions use a smaller allow-list so idle chrome stays minimal.
 */
namespace Face {

/**
 * True if the mood ring should be drawn for @p expr. All emotion
 * expressions return true; verbs/overlays defer to the legacy verb table
 * (e.g. thinking/reading, not VerbSleeping or OverlayWaking).
 */
bool moodRingShouldDraw(Expression expr);

/**
 * Draw a 6-pixel-thick ring (radii 110..115 from screen centre) in the
 * given RGB888 colour. No-op if (r,g,b) == (0,0,0). Intended to be
 * called after the face is drawn but before activity dots.
 */
void drawMoodRing(TFT_eSprite& s, uint8_t r, uint8_t g, uint8_t b);

}  // namespace Face
