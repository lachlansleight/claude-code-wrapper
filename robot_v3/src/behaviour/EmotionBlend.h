#pragma once

#include "../face/FACE_CONFIG.h"
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
 * `Face::baseTargetFor(...)` (`FaceConfig::kBaseTargets`); each field is a
 * `ParamI16` — per-field strength-weighted blend with `W==0` fallback per `06`.
 *
 * Inside any emotion rectangle, all four corners belong to the same
 * emotion → the blend collapses to that emotion's preset exactly.
 * Between rectangles, the blend produces a smooth gradient.
 *
 * The mood ring fields (`ring_*`) are blended like other FaceParams
 * and consumed by FrameController for the perimeter ring.
 */
namespace EmotionBlend {

/**
 * Compute the blended base FaceParams for the current (valence,
 * activation) point. @p v should be in [-1, +1] and @p a in [0, 1];
 * out-of-range inputs are clamped. Includes `ring_r/g/b`.
 */
Face::FaceParams blendedFaceParams(float v, float a);

/**
 * Barycentric blend of per-anchor arm motion presets at (v, a), same
 * triangulation as blendedFaceParams.
 */
Face::EmotionArmMotion blendedEmotionArmMotion(float v, float a);

/**
 * Barycentric blend of per-expression idle animation rows (blink / gaze / bob)
 * at (v, a), same triangulation as blendedFaceParams.
 */
FaceConfig::IdleAnimRow blendedIdleAnim(float v, float a);

}  // namespace EmotionBlend
