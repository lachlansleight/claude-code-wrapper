#pragma once

#include <Arduino.h>

#include "SceneTypes.h"

namespace Face {

/** Initialise smoothed emotion state (call from `Face::begin`). */
void effectiveParamsBegin();

/** Smooth toward `ctx.base_face_params`, apply verb timeline overrides, store result. */
void tickEffectiveParams(const SceneContext& ctx, uint32_t now);

/** Last combined face + arm parameters (emotion smooth + verb overrides). */
const FaceParams& effectiveFaceParams();

void invalidateEffectiveParams();

}  // namespace Face
