#pragma once

#include <stdint.h>

#include "FaceEnums.h"
#include "SceneTypes.h"

namespace Face {

/// Sparse verb face overrides sampled from authored timelines (`FACE_CONFIG.h`).
void sampleVerbTimeline(Expression verb, uint32_t time_in_verb_ms, bool* hasField,
                        ParamI16* fieldVals);

}  // namespace Face
