#pragma once

#include <Arduino.h>

#include "../face/SceneTypes.h"

namespace MotionBehaviors {

void begin();
void tick(Face::Expression expression);

uint16_t periodMsFor(Face::Expression expression);

}  // namespace MotionBehaviors
