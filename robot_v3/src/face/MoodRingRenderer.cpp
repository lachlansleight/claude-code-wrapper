#include "MoodRingRenderer.h"

#include "FACE_CONFIG.h"

namespace Face {

bool moodRingShouldDraw(Expression expr) {
  if (isEmotionExpression(expr)) return true;
  return FaceConfig::moodRingEnabledVerbOrOverlay(expr);
}

void drawMoodRing(TFT_eSprite& s, uint8_t r, uint8_t g, uint8_t b) {
  if (r == 0 && g == 0 && b == 0) return;
  static constexpr int16_t kMoodRingOuterR = 115;
  static constexpr int16_t kMoodRingInnerR = 109;
  const uint16_t ringColor = rgb888To565(r, g, b);
  for (int16_t rad = kMoodRingInnerR + 1; rad <= kMoodRingOuterR; ++rad) {
    s.drawCircle(kCx, kCy, rad, ringColor);
  }
}

}  // namespace Face
