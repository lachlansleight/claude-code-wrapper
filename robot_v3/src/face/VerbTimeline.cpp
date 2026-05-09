#include "VerbTimeline.h"

#include <string.h>

#include "FACE_CONFIG.h"

namespace Face {

namespace {

const FaceConfig::SparseVerbTimeline* tableFor(Expression v) {
  for (size_t i = 0; i < FaceConfig::kVerbTimelineCount; ++i) {
    if (FaceConfig::kVerbTimelines[i].verb == v) {
      return &FaceConfig::kVerbTimelines[i];
    }
  }
  return nullptr;
}

}  // namespace

void sampleVerbTimeline(Expression verb, uint32_t /*time_in_verb_ms*/, bool* hasField,
                        ParamI16* fieldVals) {
  memset(hasField, 0, (size_t)FieldIndex::Count * sizeof(bool));
  memset(fieldVals, 0, (size_t)FieldIndex::Count * sizeof(ParamI16));

  const FaceConfig::SparseVerbTimeline* tab = tableFor(verb);
  if (!tab) return;

  for (uint8_t i = 0; i < tab->count; ++i) {
    const uint8_t fi = tab->o[i].field;
    if (fi >= (uint8_t)FieldIndex::Count) continue;
    hasField[fi] = true;
    fieldVals[fi] = ParamI16{tab->o[i].value, tab->o[i].strength};
  }
}

}  // namespace Face
