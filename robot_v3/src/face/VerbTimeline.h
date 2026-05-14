#pragma once

#include <stdint.h>

#include "FaceEnums.h"
#include "SceneTypes.h"

namespace Face {

/// Cross-fade duration when the active verb expression changes (or when
/// transitioning to/from a non-verb state). Symmetric for in/out and
/// verb-to-verb. Mirrors `kVerbTransitionMs` in the simulator.
constexpr uint32_t kVerbTransitionDurMs = 500;

/// True for the six verb expressions that use `FaceConfig::kVerbTimelines`.
bool expressionUsesVerbTimeline(Expression e);

/// Sample keyframed verb face overrides from `FACE_CONFIG_DATA.h::kVerbTimelines`.
void sampleVerbTimeline(Expression verb, uint32_t time_in_verb_ms, bool* hasField,
                        ParamI16* fieldVals);

/// Sample the effective verb timeline with smooth transitions. On change of
/// `currentVerbExpression` vs the previous call, snapshots the in-flight
/// effective output and cross-fades over `kVerbTransitionDurMs` to the new
/// target. Pass any non-verb expression (e.g. an emotion or
/// `Expression::Count`) to ramp the verb influence out toward an empty
/// sample. Per-field blend rules:
///   * both sides override → lerp value AND strength by t
///   * only `from` overrides → keep value, scale strength by (1 - t)
///   * only `to` overrides → keep value, scale strength by t
///   * a field whose blended strength rounds to 0 is dropped (`hasField` false)
void sampleEffectiveVerb(Expression currentVerbExpression, uint32_t nowMs,
                         uint32_t timeInVerbMs, bool* hasField, ParamI16* fieldVals);

/// Reset transition snapshot at boot. Call from `Face::begin()`.
void resetVerbTransition();

/// Fraction `[0, 1]` of the current verb cross-fade. Returns 1.0 when no
/// transition is in flight (or before any verb has been set). Used by the
/// modification pass (bob amplitude, gaze) to glide expression-keyed values
/// across the same 500 ms window the timeline blend uses.
float verbTransitionT(uint32_t nowMs);

}  // namespace Face
