#include "MoodRingRenderer.h"

#include "SceneTypes.h"

namespace Face {

static constexpr int16_t kMoodRingOuterR = 115;
static constexpr int16_t kMoodRingInnerR = 109;

bool moodRingEnabledFor(Personality::State st) {
  return st == Personality::THINKING || st == Personality::READING ||
         st == Personality::WRITING || st == Personality::EXECUTING ||
         st == Personality::EXECUTING_LONG || st == Personality::FINISHED ||
         st == Personality::EXCITED || st == Personality::BLOCKED ||
         st == Personality::WANTS_ATTENTION;
}

void drawMoodRing(TFT_eSprite& s, uint8_t r, uint8_t g, uint8_t b) {
  if (r == 0 && g == 0 && b == 0) return;
  const uint16_t ringColor = rgb888To565(r, g, b);
  for (int16_t rad = kMoodRingInnerR + 1; rad <= kMoodRingOuterR; ++rad) {
    s.drawCircle(kCx, kCy, rad, ringColor);
  }
}

}  // namespace Face
