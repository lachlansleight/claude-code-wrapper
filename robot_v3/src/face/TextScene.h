#pragma once

#include <TFT_eSPI.h>

#include "SceneTypes.h"

/**
 * @file TextScene.h
 * @brief Text-mode and debug-mode scene renderer.
 *
 * Two related layouts share this module:
 *  - **Text mode** — circular-clipped status display. A bold title
 *    line ("Thinking", "Done", an activity verb) at the top of the
 *    disc, a subtitle showing either the elapsed seconds (Thinking)
 *    or wall-clock time (Done) or the current tool detail, then a
 *    body region beneath a divider that wraps the streaming agent
 *    text inside the visible circle.
 *  - **Debug mode** — full-screen left-aligned dump of the live
 *    behaviour state: effective expression, raw valence/arousal,
 *    snapped emotion (with hysteresis), verb chain, overlay state
 *    and held emotion drivers.
 *
 * The layout chooses between the two via `ctx.render_mode`.
 */
namespace Face {

/**
 * Render the text-or-debug scene into @p s. Clears the sprite. In
 * sleep state, replaces the title with "Zzz..." and suppresses the
 * subtitle and body. Wraps body text along the circular clip mask
 * inferred from `kCircleRadius` so wrapped lines don't run off the
 * round display.
 */
void renderTextScene(TFT_eSprite& s, const SceneRenderState& renderState, const SceneContext& ctx,
                     uint32_t now);

}  // namespace Face
