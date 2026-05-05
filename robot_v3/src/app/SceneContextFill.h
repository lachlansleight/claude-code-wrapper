#pragma once

#include "../face/SceneTypes.h"

/**
 * @file SceneContextFill.h
 * @brief Build a `Face::SceneContext` snapshot from live module state.
 *
 * Every frame, FrameController needs a flat read-only snapshot of "the
 * world" — agent state, current verb, current emotion, palette,
 * settings version. SceneContextFill is the single place that knows
 * how to assemble it. By keeping the assembly here:
 *
 *  - Renderers stay decoupled from the source modules.
 *  - The snapshot is consistent — nothing it depends on can change
 *    mid-frame.
 *  - The "verb beats emotion" composition rule lives in one place:
 *    if a verb (or overlay) is active, the effective expression is
 *    the verb-derived one; otherwise it's the emotion-derived one.
 *
 * Strings are sanitised through AsciiCopy on the way in so renderers
 * can blit them directly to the display.
 */
namespace SceneContextFill {

/**
 * Populate @p out from `AgentEvents::state()`,
 * `VerbSystem`/`EmotionSystem` debug state, and `Settings`. Zero-fills
 * @p out first; safe to call every frame.
 */
void fill(Face::SceneContext& out);

}  // namespace SceneContextFill
