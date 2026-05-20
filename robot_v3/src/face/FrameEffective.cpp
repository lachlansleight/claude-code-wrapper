#include "FrameEffective.h"

#include <math.h>

#include "FACE_CONFIG.h"
#include "VerbTimeline.h"

namespace Face {

namespace {

static FaceParams sSmoothedEmotion;
static FaceParams sEffective;
static uint32_t sLastSmoothMs = 0;

static const FaceConfig::FrameAnimConfig& animCfg() { return FaceConfig::kFrameAnim; }

}  // namespace

void effectiveParamsBegin() {
  resetVerbTransition();
  sSmoothedEmotion = FaceConfig::kBaseTargets[0];
  sEffective = sSmoothedEmotion;
  sLastSmoothMs = 0;
}

void invalidateEffectiveParams() { sLastSmoothMs = 0; }

const FaceParams& effectiveFaceParams() { return sEffective; }

void tickEffectiveParams(const SceneContext& ctx, uint32_t now) {
  const uint32_t dt = (sLastSmoothMs == 0) ? 0 : (now - sLastSmoothMs);
  sLastSmoothMs = now;
  const float emoAlpha =
      1.0f - expf(-(float)dt / animCfg().emotion_geometry_smooth_tau_ms);
  smoothFaceValuesToward(sSmoothedEmotion, ctx.base_face_params, emoAlpha);

  const Expression expr = ctx.effective_expression;

  constexpr uint8_t kFieldCount = (uint8_t)FieldIndex::Count;
  bool verbHas[kFieldCount];
  ParamI16 verbVals[kFieldCount];
  sampleEffectiveVerb(expr, now, ctx.verb_time_in_current_ms, verbHas, verbVals);
  sEffective = combineEmotionVerbFace(sSmoothedEmotion, verbHas, verbVals);
}

}  // namespace Face
