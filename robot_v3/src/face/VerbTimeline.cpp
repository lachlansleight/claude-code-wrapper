#include "VerbTimeline.h"

#include <string.h>

namespace Face {

namespace {

struct SparseOverride {
  uint8_t field;  ///< `FieldIndex` discriminant
  int16_t value;
  uint8_t strength;
};

struct SparseVerbTimeline {
  Expression verb;
  uint8_t count;
  SparseOverride o[32];
};

#define OI(I, V) SparseOverride{ (uint8_t)(I), (int16_t)(V), 100 }

// Single static pose per verb (legacy kBaseTargets geometry, PR B).
static constexpr SparseVerbTimeline kThinking = {
    Expression::VerbThinking,
    28,
    {
        OI(FieldIndex::EyeDy, 0),
        OI(FieldIndex::EyeRx, 30),
        OI(FieldIndex::EyeTopApex, -30),
        OI(FieldIndex::EyeTopCorner, 0),
        OI(FieldIndex::EyeBotApex, 30),
        OI(FieldIndex::EyeBotCorner, 0),
        OI(FieldIndex::EyeThick, 3),
        OI(FieldIndex::EyeWaveAmp, 0),
        OI(FieldIndex::EyeWaveFreq, 0),
        OI(FieldIndex::EyeWaveSpeed, 0),
        OI(FieldIndex::PupilDx, 7),
        OI(FieldIndex::PupilDy, -9),
        OI(FieldIndex::PupilR, 15),
        OI(FieldIndex::MouthDy, 0),
        OI(FieldIndex::MouthRx, 11),
        OI(FieldIndex::MouthTopApex, 3),
        OI(FieldIndex::MouthTopCorner, 0),
        OI(FieldIndex::MouthBotApex, 3),
        OI(FieldIndex::MouthBotCorner, 0),
        OI(FieldIndex::MouthThick, 3),
        OI(FieldIndex::MouthWaveAmp, 0),
        OI(FieldIndex::MouthWaveFreq, 0),
        OI(FieldIndex::MouthWaveSpeed, 0),
        OI(FieldIndex::FaceRot, -10),
        OI(FieldIndex::FaceY, 0),
        OI(FieldIndex::RingR, 36),
        OI(FieldIndex::RingG, 56),
        OI(FieldIndex::RingB, 120),
    },
};

static constexpr SparseVerbTimeline kReading = {
    Expression::VerbReading,
    28,
    {
        OI(FieldIndex::EyeDy, 0),
        OI(FieldIndex::EyeRx, 28),
        OI(FieldIndex::EyeTopApex, -26),
        OI(FieldIndex::EyeTopCorner, 0),
        OI(FieldIndex::EyeBotApex, 26),
        OI(FieldIndex::EyeBotCorner, 0),
        OI(FieldIndex::EyeThick, 3),
        OI(FieldIndex::EyeWaveAmp, 0),
        OI(FieldIndex::EyeWaveFreq, 0),
        OI(FieldIndex::EyeWaveSpeed, 0),
        OI(FieldIndex::PupilDx, 0),
        OI(FieldIndex::PupilDy, 8),
        OI(FieldIndex::PupilR, 12),
        OI(FieldIndex::MouthDy, 0),
        OI(FieldIndex::MouthRx, 9),
        OI(FieldIndex::MouthTopApex, 3),
        OI(FieldIndex::MouthTopCorner, 0),
        OI(FieldIndex::MouthBotApex, 3),
        OI(FieldIndex::MouthBotCorner, 0),
        OI(FieldIndex::MouthThick, 3),
        OI(FieldIndex::MouthWaveAmp, 0),
        OI(FieldIndex::MouthWaveFreq, 0),
        OI(FieldIndex::MouthWaveSpeed, 0),
        OI(FieldIndex::FaceRot, 0),
        OI(FieldIndex::FaceY, 12),
        OI(FieldIndex::RingR, 78),
        OI(FieldIndex::RingG, 146),
        OI(FieldIndex::RingB, 210),
    },
};

static constexpr SparseVerbTimeline kWriting = {
    Expression::VerbWriting,
    28,
    {
        OI(FieldIndex::EyeDy, 0),
        OI(FieldIndex::EyeRx, 30),
        OI(FieldIndex::EyeTopApex, -26),
        OI(FieldIndex::EyeTopCorner, 0),
        OI(FieldIndex::EyeBotApex, 26),
        OI(FieldIndex::EyeBotCorner, 0),
        OI(FieldIndex::EyeThick, 3),
        OI(FieldIndex::EyeWaveAmp, 0),
        OI(FieldIndex::EyeWaveFreq, 0),
        OI(FieldIndex::EyeWaveSpeed, 0),
        OI(FieldIndex::PupilDx, 0),
        OI(FieldIndex::PupilDy, -8),
        OI(FieldIndex::PupilR, 15),
        OI(FieldIndex::MouthDy, 0),
        OI(FieldIndex::MouthRx, 15),
        OI(FieldIndex::MouthTopApex, 0),
        OI(FieldIndex::MouthTopCorner, 0),
        OI(FieldIndex::MouthBotApex, 14),
        OI(FieldIndex::MouthBotCorner, 0),
        OI(FieldIndex::MouthThick, 3),
        OI(FieldIndex::MouthWaveAmp, 0),
        OI(FieldIndex::MouthWaveFreq, 0),
        OI(FieldIndex::MouthWaveSpeed, 0),
        OI(FieldIndex::FaceRot, 0),
        OI(FieldIndex::FaceY, 0),
        OI(FieldIndex::RingR, 104),
        OI(FieldIndex::RingG, 118),
        OI(FieldIndex::RingB, 228),
    },
};

static constexpr SparseVerbTimeline kExecuting = {
    Expression::VerbExecuting,
    28,
    {
        OI(FieldIndex::EyeDy, 0),
        OI(FieldIndex::EyeRx, 30),
        OI(FieldIndex::EyeTopApex, -16),
        OI(FieldIndex::EyeTopCorner, 0),
        OI(FieldIndex::EyeBotApex, 16),
        OI(FieldIndex::EyeBotCorner, 0),
        OI(FieldIndex::EyeThick, 3),
        OI(FieldIndex::EyeWaveAmp, 0),
        OI(FieldIndex::EyeWaveFreq, 0),
        OI(FieldIndex::EyeWaveSpeed, 0),
        OI(FieldIndex::PupilDx, 0),
        OI(FieldIndex::PupilDy, -4),
        OI(FieldIndex::PupilR, 10),
        OI(FieldIndex::MouthDy, 0),
        OI(FieldIndex::MouthRx, 9),
        OI(FieldIndex::MouthTopApex, 2),
        OI(FieldIndex::MouthTopCorner, 0),
        OI(FieldIndex::MouthBotApex, 2),
        OI(FieldIndex::MouthBotCorner, 0),
        OI(FieldIndex::MouthThick, 3),
        OI(FieldIndex::MouthWaveAmp, 0),
        OI(FieldIndex::MouthWaveFreq, 0),
        OI(FieldIndex::MouthWaveSpeed, 0),
        OI(FieldIndex::FaceRot, 0),
        OI(FieldIndex::FaceY, 0),
        OI(FieldIndex::RingR, 156),
        OI(FieldIndex::RingG, 64),
        OI(FieldIndex::RingB, 216),
    },
};

static constexpr SparseVerbTimeline kStraining = {
    Expression::VerbStraining,
    28,
    {
        OI(FieldIndex::EyeDy, 0),
        OI(FieldIndex::EyeRx, 30),
        OI(FieldIndex::EyeTopApex, -22),
        OI(FieldIndex::EyeTopCorner, 0),
        OI(FieldIndex::EyeBotApex, 22),
        OI(FieldIndex::EyeBotCorner, 0),
        OI(FieldIndex::EyeThick, 3),
        OI(FieldIndex::EyeWaveAmp, 0),
        OI(FieldIndex::EyeWaveFreq, 0),
        OI(FieldIndex::EyeWaveSpeed, 0),
        OI(FieldIndex::PupilDx, 0),
        OI(FieldIndex::PupilDy, -3),
        OI(FieldIndex::PupilR, 10),
        OI(FieldIndex::MouthDy, 0),
        OI(FieldIndex::MouthRx, 18),
        OI(FieldIndex::MouthTopApex, 0),
        OI(FieldIndex::MouthTopCorner, 0),
        OI(FieldIndex::MouthBotApex, 0),
        OI(FieldIndex::MouthBotCorner, 0),
        OI(FieldIndex::MouthThick, 3),
        OI(FieldIndex::MouthWaveAmp, 4),
        OI(FieldIndex::MouthWaveFreq, 100),
        OI(FieldIndex::MouthWaveSpeed, 360),
        OI(FieldIndex::FaceRot, 0),
        OI(FieldIndex::FaceY, 0),
        OI(FieldIndex::RingR, 210),
        OI(FieldIndex::RingG, 75),
        OI(FieldIndex::RingB, 220),
    },
};

static constexpr SparseVerbTimeline kSleeping = {
    Expression::VerbSleeping,
    28,
    {
        OI(FieldIndex::EyeDy, 8),
        OI(FieldIndex::EyeRx, 26),
        OI(FieldIndex::EyeTopApex, -2),
        OI(FieldIndex::EyeTopCorner, 0),
        OI(FieldIndex::EyeBotApex, 2),
        OI(FieldIndex::EyeBotCorner, 0),
        OI(FieldIndex::EyeThick, 3),
        OI(FieldIndex::EyeWaveAmp, 0),
        OI(FieldIndex::EyeWaveFreq, 0),
        OI(FieldIndex::EyeWaveSpeed, 0),
        OI(FieldIndex::PupilDx, 0),
        OI(FieldIndex::PupilDy, 0),
        OI(FieldIndex::PupilR, 15),
        OI(FieldIndex::MouthDy, 0),
        OI(FieldIndex::MouthRx, 9),
        OI(FieldIndex::MouthTopApex, 0),
        OI(FieldIndex::MouthTopCorner, 0),
        OI(FieldIndex::MouthBotApex, 0),
        OI(FieldIndex::MouthBotCorner, 0),
        OI(FieldIndex::MouthThick, 3),
        OI(FieldIndex::MouthWaveAmp, 0),
        OI(FieldIndex::MouthWaveFreq, 0),
        OI(FieldIndex::MouthWaveSpeed, 0),
        OI(FieldIndex::FaceRot, 0),
        OI(FieldIndex::FaceY, 0),
        OI(FieldIndex::RingR, 0),
        OI(FieldIndex::RingG, 0),
        OI(FieldIndex::RingB, 0),
    },
};

#undef OI

const SparseVerbTimeline* tableFor(Expression v) {
  switch (v) {
    case Expression::VerbThinking:
      return &kThinking;
    case Expression::VerbReading:
      return &kReading;
    case Expression::VerbWriting:
      return &kWriting;
    case Expression::VerbExecuting:
      return &kExecuting;
    case Expression::VerbStraining:
      return &kStraining;
    case Expression::VerbSleeping:
      return &kSleeping;
    default:
      return nullptr;
  }
}

}  // namespace

void sampleVerbTimeline(Expression verb, uint32_t /*time_in_verb_ms*/, bool* hasField,
                        ParamI16* fieldVals) {
  memset(hasField, 0, (size_t)FieldIndex::Count * sizeof(bool));
  memset(fieldVals, 0, (size_t)FieldIndex::Count * sizeof(ParamI16));

  const SparseVerbTimeline* tab = tableFor(verb);
  if (!tab) return;

  for (uint8_t i = 0; i < tab->count; ++i) {
    const uint8_t fi = tab->o[i].field;
    if (fi >= (uint8_t)FieldIndex::Count) continue;
    hasField[fi] = true;
    fieldVals[fi] = ParamI16{tab->o[i].value, tab->o[i].strength};
  }
}

}  // namespace Face
