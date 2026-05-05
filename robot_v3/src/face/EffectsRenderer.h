#pragma once

#include <TFT_eSPI.h>

/**
 * @file EffectsRenderer.h
 * @brief Procedural "code stream" backdrop effects for read/write states.
 *
 * Behind the face during VerbReading and VerbWriting we paint two
 * simulated text streams: short coloured token-runs scrolling
 * vertically, hash-derived per-line so the layout looks like
 * indented code without ever decoding real content. The left half
 * (read) scrolls fast on small lines; the right half (write) scrolls
 * slower on chunkier lines.
 *
 * Both effects are gated by a scalar alpha that FrameController
 * smooths in/out as the verb changes, so transitions don't pop.
 */
namespace Face {

/**
 * Paint both stream effects with the supplied alphas in [0, 1].
 * @p readAlpha drives the left half; @p writeAlpha drives the right
 * half. Each stream is a no-op below ~0.01 alpha. Should be called
 * after the face background fill and before the face geometry.
 */
void drawEffects(TFT_eSprite& s, uint32_t now, float readAlpha, float writeAlpha);

}  // namespace Face
