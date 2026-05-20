#include "MotionBehaviors.h"

#include "../face/FrameEffective.h"
#include "../face/FACE_CONFIG.h"
#include "Motion.h"

namespace MotionBehaviors {

static constexpr int8_t kSafeMin = -45;
static constexpr int8_t kSafeMax = 45;

void begin() {
  Motion::setSafeRange(kSafeMin, kSafeMax);
}

void tick(const Face::SceneContext& ctx) {
  (void)ctx;
  const Face::FaceParams& p = Face::effectiveFaceParams();

  int16_t lo = p.arm_min_deg.value;
  int16_t hi = p.arm_max_deg.value;
  if (lo > hi) {
    const int16_t t = lo;
    lo = hi;
    hi = t;
  }

  const float periodS = p.arm_period_ms.value < 50 ? 0.05f : (float)p.arm_period_ms.value / 1000.0f;
  const float intervalS =
      p.arm_interval_ms.value < 0 ? 0.0f : (float)p.arm_interval_ms.value / 1000.0f;

  Motion::syncEmotionArmLayer(true, lo, hi, periodS, intervalS);
}

uint16_t periodMsFor(Face::Expression expression) {
  (void)expression;
  return 0;
}

uint16_t periodMsForContext(const Face::SceneContext& ctx) {
  (void)ctx;
  return 0;
}

}  // namespace MotionBehaviors
