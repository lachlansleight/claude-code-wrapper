#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

/**
 * @file ActivityDots.h
 * @brief Curved dot rings tracking per-turn read/write tool counts.
 *
 * Around the bottom of the face we paint an arc of dots — one per
 * tool invocation in the current turn. Reads sit at the bottom of the
 * circle (arc centred at +π/2), writes at the top (-π/2). The arc
 * widens and the dots shrink as the count grows, so a busy turn shows
 * a long band of small dots; a quiet one shows a few large ones.
 *
 * When a turn ends and the face decays from Happy → Neutral, the dots
 * fade out over ~280 ms — the count is captured into
 * `SceneRenderState.fade_*_count` and scaled toward zero.
 */
namespace Face {

/**
 * Draw the activity-dot overlay. Hidden during VerbSleeping and during
 * Neutral-without-fade. Driven entirely by @p ctx
 * (read/write_tools_this_turn) plus, when a fade is active, the frozen
 * counts in @p renderState.
 */
void drawActivityDots(TFT_eSprite& s, const SceneRenderState& renderState, const SceneContext& ctx,
                      uint32_t now);

}  // namespace Face
