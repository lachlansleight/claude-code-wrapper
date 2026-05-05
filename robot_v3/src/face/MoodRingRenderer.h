#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

/**
 * @file MoodRingRenderer.h
 * @brief Optional thin coloured ring around the face perimeter.
 *
 * The mood ring is the only outward-facing colour signal for emotion /
 * verb in face mode. It is enabled per-expression — the steady "happy"
 * face has none (the smile carries it), but verb states and high
 * emotions get one.
 */
namespace Face {

/**
 * True if @p expr opts into a mood ring. The current set:
 *  - All verb states except VerbSleeping,
 *  - Joyful / Excited / Sad,
 *  - OverlayAttention.
 *
 * Excluded: Neutral, Happy, OverlayWaking, VerbSleeping.
 */
bool moodRingEnabledFor(Expression expr);

/**
 * Draw a 6-pixel-thick ring (radii 110..115 from screen centre) in the
 * given RGB888 colour. No-op if (r,g,b) == (0,0,0). Intended to be
 * called after the face is drawn but before activity dots.
 */
void drawMoodRing(TFT_eSprite& s, uint8_t r, uint8_t g, uint8_t b);

}  // namespace Face
