#pragma once

#include "../face/SceneTypes.h"

/**
 * @file EmotionBlend.h
 * @brief Continuous barycentric blend of emotion FaceParams presets.
 *
 * Replaces the snap-then-lookup base-layer behaviour: instead of
 * picking one `Face::Expression` from the snapped emotion region and
 * using `kBaseTargets[expr]` directly, this module treats the four
 * corners of each emotion rectangle as anchor points in (v, a) space,
 * triangulates the anchor cloud (offline, see
 * `EmotionTriangulation.h`), and per-frame finds the triangle that
 * contains the current (v, a) point. The three anchors of that
 * triangle each map to one of the emotion presets in
 * `Face::baseTargetFor(...)`; their `FaceParams` are blended per-field
 * with the barycentric weights.
 *
 * Inside any emotion rectangle, all four corners belong to the same
 * emotion → the blend collapses to that emotion's preset exactly.
 * Between rectangles, the blend produces a smooth gradient.
 *
 * The mood ring fields (`ring_*`) are not produced here; the colour
 * pipeline still uses the snapped emotion via FrameController.
 */
namespace EmotionBlend {

/**
 * Compute the blended base FaceParams for the current (valence,
 * activation) point. @p v should be in [-1, +1] and @p a in [0, 1];
 * out-of-range inputs are clamped.
 *
 * Output `ring_r/g/b` are blended too but should be considered
 * undefined here — FrameController overwrites them from the snapped
 * expression's palette colour.
 */
Face::FaceParams blendedFaceParams(float v, float a);

/**
 * Barycentric blend of per-anchor arm motion presets at (v, a), same
 * triangulation as blendedFaceParams.
 */
Face::EmotionArmMotion blendedEmotionArmMotion(float v, float a);

}  // namespace EmotionBlend
