#pragma once

#include <Arduino.h>

#include "SceneTypes.h"

namespace Face {

void begin();
void tick(const SceneContext& ctx);
void invalidate();

}  // namespace Face
