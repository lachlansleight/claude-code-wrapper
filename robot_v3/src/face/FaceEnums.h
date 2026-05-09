#pragma once

#include "FACE_CONFIG_DATA.h"

namespace Face {

/// True for expressions driven by the continuous (v, a) emotion layer.
inline bool isEmotionExpression(Expression s) {
  const uint8_t idx = (uint8_t)s;
  if (idx >= (uint8_t)Expression::Count) return false;
  return FaceConfig::kExpressionIsEmotion[idx];
}

}  // namespace Face
