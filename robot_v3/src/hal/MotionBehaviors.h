#pragma once

#include <Arduino.h>

#include "../face/SceneTypes.h"

/**
 * @file MotionBehaviors.h
 * @brief Drives the arm from effective blended `FaceParams` arm fields.
 *
 * `Face::tickEffectiveParams` must run before `MotionBehaviors::tick` each loop.
 * Arm motion is always the emotion-layer sine arch (min→max→min, dwell) using
 * `arm_min_deg`, `arm_max_deg`, `arm_period_ms`, and `arm_interval_ms`.
 */
namespace MotionBehaviors {

void begin();

void tick(const Face::SceneContext& ctx);

/** Deprecated: body bob uses live servo position, not arm period. */
uint16_t periodMsFor(Face::Expression expression);

/** Deprecated: body bob uses live servo position, not arm period. */
uint16_t periodMsForContext(const Face::SceneContext& ctx);

}  // namespace MotionBehaviors
