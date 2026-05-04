#include "SceneTypes.h"

namespace Face {

const char* expressionName(Expression e) {
  switch (e) {
    case Expression::Neutral:
      return "neutral";
    case Expression::Happy:
      return "happy";
    case Expression::Excited:
      return "excited";
    case Expression::Joyful:
      return "joyful";
    case Expression::Sad:
      return "sad";
    case Expression::VerbThinking:
      return "verb_thinking";
    case Expression::VerbReading:
      return "verb_reading";
    case Expression::VerbWriting:
      return "verb_writing";
    case Expression::VerbExecuting:
      return "verb_executing";
    case Expression::VerbStraining:
      return "verb_straining";
    case Expression::VerbSleeping:
      return "verb_sleeping";
    case Expression::OverlayWaking:
      return "overlay_waking";
    case Expression::OverlayAttention:
      return "overlay_attention";
    default:
      return "?";
  }
}

}  // namespace Face
