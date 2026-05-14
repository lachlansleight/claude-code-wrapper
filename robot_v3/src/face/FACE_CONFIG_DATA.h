#pragma once

/**
 * @file FACE_CONFIG_DATA.h
 * @brief Editor-exportable face data only — tables, no helpers.
 *
 * Hand-maintained until the Phase 2 editor emits this file. Firmware helpers
 * live in `FACE_CONFIG.h`. Python `gen_emotion_triangulation.py` parses
 * `kEmotionPoints` from here.
 */

#include "FacePrimitives.h"

namespace Face {

enum class Expression : uint8_t {
  Neutral = 0,
  Happy,
  Excited,
  Joyful,
  Sad,
  VerbThinking,
  VerbReading,
  VerbWriting,
  VerbExecuting,
  VerbStraining,
  VerbSleeping,
  OverlayWaking,
  OverlayAttention,
  Sleepy,
  Distressed,
  Blissed,
  Depressed,
  Shocked,
  Disappointed,
  Cheeky,
  Gleeful,
  Frustrated,
  Count
};

}  // namespace Face

namespace EmotionSystem {

enum class NamedEmotion : uint8_t {
  Neutral = 0,
  Happy,
  Excited,
  Joyful,
  Sad,
  Sleepy,
  Distressed,
  Blissed,
  Depressed,
  Shocked,
  Disappointed,
  Cheeky,
  Gleeful,
  Frustrated,
  Count
};

}  // namespace EmotionSystem

namespace FaceConfig {

static constexpr const char* kExpressionNames[(size_t)Face::Expression::Count] = {
    "neutral",          "happy",          "excited",      "joyful",      "sad",          "verb_thinking",
    "verb_reading",     "verb_writing",   "verb_executing","verb_straining","verb_sleeping","overlay_waking",
    "overlay_attention","sleepy",         "distressed",   "blissed",     "depressed",    "shocked",
    "disappointed",     "cheeky",         "gleeful",      "frustrated",
};

static constexpr bool kExpressionIsEmotion[(size_t)Face::Expression::Count] = {
    true,  true,  true,  true,  true,  false, false, false, false, false, false,
    false, false, true,  true,  true,  true,  true,  true,  true,  true,  true,
};

static constexpr const char* kEmotionNames[(size_t)EmotionSystem::NamedEmotion::Count] = {
    "neutral",     "happy",    "excited",   "joyful",     "sad",        "sleepy",
    "distressed",  "blissed",  "depressed", "shocked",    "disappointed","cheeky",
    "gleeful",     "frustrated",
};

// ─── (v, a) anchors for NamedEmotion (triangulation + discrete snap) ─────

struct EmotionPoint {
  float v;
  float a;
};

static constexpr EmotionPoint kEmotionPoints[(size_t)EmotionSystem::NamedEmotion::Count] = {
    {+0.0f, 0.5f},    // Neutral
    {+0.5f, 0.5f},    // Happy
    {+1.0f, 0.6f},    // Excited
    {+1.0f, 1.0f},    // Joyful
    {-0.5f, 0.5f},    // Sad
    {-0.2f, 0.0f},    // Sleepy
    {-1.0f, 1.0f},    // Distressed
    {+1.0f, 0.0f},    // Blissed
    {-1.0f, 0.0f},    // Depressed
    {-0.3f, 1.0f},    // Shocked
    {-1.0f, 0.3f},    // Disappointed
    {+0.5f, 0.7f},    // Cheeky
    {+0.6f, 1.0f},    // Gleeful
    {-0.6f, 0.8f},    // Frustrated
};

/// Tie-break when two anchors are equidistant (earlier wins).
static constexpr EmotionSystem::NamedEmotion kPickOrder[] = {
    EmotionSystem::NamedEmotion::Gleeful,
    EmotionSystem::NamedEmotion::Cheeky,
    EmotionSystem::NamedEmotion::Sleepy,
    EmotionSystem::NamedEmotion::Distressed,
    EmotionSystem::NamedEmotion::Frustrated,
    EmotionSystem::NamedEmotion::Disappointed,
    EmotionSystem::NamedEmotion::Blissed,
    EmotionSystem::NamedEmotion::Depressed,
    EmotionSystem::NamedEmotion::Shocked,
    EmotionSystem::NamedEmotion::Neutral,
    EmotionSystem::NamedEmotion::Happy,
    EmotionSystem::NamedEmotion::Excited,
    EmotionSystem::NamedEmotion::Joyful,
    EmotionSystem::NamedEmotion::Sad,
};

static constexpr Face::Expression
    kNamedEmotionToExpression[(size_t)EmotionSystem::NamedEmotion::Count] = {
        Face::Expression::Neutral,      Face::Expression::Happy,     Face::Expression::Excited,
        Face::Expression::Joyful,       Face::Expression::Sad,       Face::Expression::Sleepy,
        Face::Expression::Distressed,   Face::Expression::Blissed,   Face::Expression::Depressed,
        Face::Expression::Shocked,      Face::Expression::Disappointed,
        Face::Expression::Cheeky,       Face::Expression::Gleeful,   Face::Expression::Frustrated,
};

// ─── Per-expression geometry (ParamI16) ───────────────────────────────────
// Emotion rows: full strength presets for Delaunay blend. Verb / overlay rows
// duplicate the same tuned geometry as kVerbTimelines keyframe 0 (hand-tuned
// open_amt / arc_amt model; see FaceRenderer::arcDerived).
#ifndef FACE_P
#define FACE_P(V) ::Face::ParamI16{ (int16_t)(V), 100 }
#endif
static const Face::FaceParams kBaseTargets[(uint8_t)Face::Expression::Count] = {
    /* Neutral */
    {FACE_P(3), FACE_P(30), FACE_P(26), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(3), FACE_P(15), FACE_P(2), FACE_P(15), FACE_P(1), FACE_P(0), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Happy */
    {FACE_P(7), FACE_P(30), FACE_P(23), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(5), FACE_P(16), FACE_P(2), FACE_P(24), FACE_P(2), FACE_P(20), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(7), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Excited */
    {FACE_P(-1), FACE_P(29), FACE_P(28), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(17), FACE_P(3), FACE_P(27), FACE_P(2), FACE_P(48), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(3), FACE_P(40), FACE_P(252), FACE_P(79)},
    /* Joyful */
    {FACE_P(-11), FACE_P(20), FACE_P(2), FACE_P(-64), FACE_P(4), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(14), FACE_P(2), FACE_P(37), FACE_P(14), FACE_P(69), FACE_P(4),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(-13), FACE_P(255), FACE_P(228), FACE_P(38)},
    /* Sad */
    {FACE_P(7), FACE_P(28), FACE_P(15), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(3), FACE_P(11), FACE_P(-6), FACE_P(20), FACE_P(1), FACE_P(-14), FACE_P(3),
     FACE_P(0), FACE_P(1), FACE_P(3), FACE_P(0), FACE_P(6), FACE_P(4), FACE_P(1), FACE_P(3)},
    /* VerbThinking */
    {FACE_P(2), FACE_P(28), FACE_P(26), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(9), FACE_P(-8), FACE_P(15), FACE_P(1), FACE_P(12), FACE_P(1), FACE_P(15), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(-12), FACE_P(2), FACE_P(36), FACE_P(56), FACE_P(120)},
    /* VerbReading */
    {FACE_P(1), FACE_P(27), FACE_P(24), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(13), FACE_P(15), FACE_P(0), FACE_P(13), FACE_P(1), FACE_P(19), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(18), FACE_P(78), FACE_P(146), FACE_P(210)},
    /* VerbWriting */
    {FACE_P(0), FACE_P(28), FACE_P(25), FACE_P(24), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(-9), FACE_P(15), FACE_P(0), FACE_P(19), FACE_P(7), FACE_P(31), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(-13), FACE_P(104), FACE_P(118), FACE_P(228)},
    /* VerbExecuting */
    {FACE_P(0), FACE_P(30), FACE_P(13), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(-3), FACE_P(11), FACE_P(0), FACE_P(11), FACE_P(1), FACE_P(13), FACE_P(3),
     FACE_P(0), FACE_P(55), FACE_P(0), FACE_P(0), FACE_P(1), FACE_P(156), FACE_P(64), FACE_P(216)},
    /* VerbStraining */
    {FACE_P(1), FACE_P(30), FACE_P(22), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(3), FACE_P(25),
     FACE_P(0), FACE_P(-3), FACE_P(10), FACE_P(1), FACE_P(18), FACE_P(1), FACE_P(0), FACE_P(3),
     FACE_P(4), FACE_P(96), FACE_P(364), FACE_P(0), FACE_P(0), FACE_P(210), FACE_P(75), FACE_P(220)},
    /* VerbSleeping */
    {FACE_P(2), FACE_P(30), FACE_P(0), FACE_P(15), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(1),
     FACE_P(0), FACE_P(3), FACE_P(15), FACE_P(1), FACE_P(15), FACE_P(0), FACE_P(0), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(1), FACE_P(0), FACE_P(17), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* OverlayWaking */
    {FACE_P(2), FACE_P(31), FACE_P(34), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(1),
     FACE_P(0), FACE_P(3), FACE_P(13), FACE_P(1), FACE_P(9), FACE_P(12), FACE_P(0), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(1), FACE_P(0), FACE_P(-2), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* OverlayAttention */
    {FACE_P(3), FACE_P(30), FACE_P(31), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(83), FACE_P(707),
     FACE_P(0), FACE_P(3), FACE_P(12), FACE_P(0), FACE_P(17), FACE_P(13), FACE_P(26), FACE_P(1),
     FACE_P(0), FACE_P(48), FACE_P(707), FACE_P(0), FACE_P(0), FACE_P(255), FACE_P(20), FACE_P(40)},
    /* Sleepy */
    {FACE_P(15), FACE_P(28), FACE_P(17), FACE_P(24), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(-13), FACE_P(15), FACE_P(2), FACE_P(13), FACE_P(2), FACE_P(8), FACE_P(3),
     FACE_P(0), FACE_P(17), FACE_P(90), FACE_P(0), FACE_P(13), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Distressed */
    {FACE_P(5), FACE_P(30), FACE_P(30), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(7), FACE_P(10), FACE_P(-5), FACE_P(24), FACE_P(5), FACE_P(-46), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(-17), FACE_P(255), FACE_P(48), FACE_P(24)},
    /* Blissed */
    {FACE_P(4), FACE_P(20), FACE_P(0), FACE_P(24), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(15), FACE_P(8), FACE_P(26), FACE_P(6), FACE_P(29), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(7), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Depressed */
    {FACE_P(23), FACE_P(30), FACE_P(8), FACE_P(74), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(-2), FACE_P(6), FACE_P(4), FACE_P(13), FACE_P(0), FACE_P(-11), FACE_P(3),
     FACE_P(0), FACE_P(17), FACE_P(90), FACE_P(0), FACE_P(9), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Shocked */
    {FACE_P(2), FACE_P(30), FACE_P(37), FACE_P(0), FACE_P(3), FACE_P(1), FACE_P(85), FACE_P(720),
     FACE_P(0), FACE_P(3), FACE_P(9), FACE_P(15), FACE_P(17), FACE_P(13), FACE_P(0), FACE_P(1),
     FACE_P(2), FACE_P(49), FACE_P(720), FACE_P(0), FACE_P(0), FACE_P(255), FACE_P(255), FACE_P(255)},
    /* Disappointed */
    {FACE_P(5), FACE_P(21), FACE_P(0), FACE_P(27), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(3), FACE_P(8), FACE_P(-15), FACE_P(25), FACE_P(2), FACE_P(-21), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(2), FACE_P(0), FACE_P(0), FACE_P(225), FACE_P(53), FACE_P(93)},
    /* Cheeky */
    {FACE_P(-10), FACE_P(28), FACE_P(17), FACE_P(-131), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(3), FACE_P(15), FACE_P(-18), FACE_P(20), FACE_P(2), FACE_P(34), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(-3), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Gleeful */
    {FACE_P(-10), FACE_P(27), FACE_P(13), FACE_P(-137), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(1),
     FACE_P(0), FACE_P(1), FACE_P(14), FACE_P(-17), FACE_P(27), FACE_P(7), FACE_P(61), FACE_P(3),
     FACE_P(0), FACE_P(0), FACE_P(1), FACE_P(0), FACE_P(2), FACE_P(39), FACE_P(248), FACE_P(78)},
    /* Frustrated */
    {FACE_P(1), FACE_P(30), FACE_P(22), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(1), FACE_P(5),
     FACE_P(0), FACE_P(-3), FACE_P(10), FACE_P(0), FACE_P(18), FACE_P(0), FACE_P(0), FACE_P(3),
     FACE_P(4), FACE_P(96), FACE_P(348), FACE_P(0), FACE_P(1), FACE_P(212), FACE_P(75), FACE_P(212)},
};
#undef FACE_P

// ─── Verb sparse face overrides (timeline source) ─────────────────────────

struct SparseOverride {
  uint8_t field;  ///< `Face::FieldIndex` discriminant.
  int16_t value;
  uint8_t strength;
};

struct SparseVerbTimeline {
  Face::Expression verb;
  uint8_t count;
  SparseOverride o[32];
};

#define OI(I, V) SparseOverride{(uint8_t)(I), (int16_t)(V), 100}
static constexpr SparseVerbTimeline kVerbTimelines[] = {
    {Face::Expression::VerbThinking,
     24,
     {
         OI(Face::FieldIndex::EyeDy, 2),
         OI(Face::FieldIndex::EyeRx, 28),
         OI(Face::FieldIndex::EyeOpenAmt, 26),
         OI(Face::FieldIndex::EyeArcAmt, 0),
         OI(Face::FieldIndex::EyeThick, 3),
         OI(Face::FieldIndex::EyeWaveAmp, 0),
         OI(Face::FieldIndex::EyeWaveFreq, 0),
         OI(Face::FieldIndex::EyeWaveSpeed, 0),
         OI(Face::FieldIndex::PupilDx, 9),
         OI(Face::FieldIndex::PupilDy, -8),
         OI(Face::FieldIndex::PupilR, 15),
         OI(Face::FieldIndex::MouthDy, 1),
         OI(Face::FieldIndex::MouthRx, 12),
         OI(Face::FieldIndex::MouthOpenAmt, 1),
         OI(Face::FieldIndex::MouthArcAmt, 15),
         OI(Face::FieldIndex::MouthThick, 3),
         OI(Face::FieldIndex::MouthWaveAmp, 0),
         OI(Face::FieldIndex::MouthWaveFreq, 0),
         OI(Face::FieldIndex::MouthWaveSpeed, 0),
         OI(Face::FieldIndex::FaceRot, -12),
         OI(Face::FieldIndex::FaceY, 2),
         OI(Face::FieldIndex::RingR, 36),
         OI(Face::FieldIndex::RingG, 56),
         OI(Face::FieldIndex::RingB, 120),
     }},
    {Face::Expression::VerbReading,
     24,
     {
         OI(Face::FieldIndex::EyeDy, 1),
         OI(Face::FieldIndex::EyeRx, 27),
         OI(Face::FieldIndex::EyeOpenAmt, 24),
         OI(Face::FieldIndex::EyeArcAmt, 0),
         OI(Face::FieldIndex::EyeThick, 3),
         OI(Face::FieldIndex::EyeWaveAmp, 0),
         OI(Face::FieldIndex::EyeWaveFreq, 0),
         OI(Face::FieldIndex::EyeWaveSpeed, 0),
         OI(Face::FieldIndex::PupilDx, 0),
         OI(Face::FieldIndex::PupilDy, 13),
         OI(Face::FieldIndex::PupilR, 15),
         OI(Face::FieldIndex::MouthDy, 0),
         OI(Face::FieldIndex::MouthRx, 13),
         OI(Face::FieldIndex::MouthOpenAmt, 1),
         OI(Face::FieldIndex::MouthArcAmt, 19),
         OI(Face::FieldIndex::MouthThick, 3),
         OI(Face::FieldIndex::MouthWaveAmp, 0),
         OI(Face::FieldIndex::MouthWaveFreq, 0),
         OI(Face::FieldIndex::MouthWaveSpeed, 0),
         OI(Face::FieldIndex::FaceRot, 0),
         OI(Face::FieldIndex::FaceY, 18),
         OI(Face::FieldIndex::RingR, 78),
         OI(Face::FieldIndex::RingG, 146),
         OI(Face::FieldIndex::RingB, 210),
     }},
    {Face::Expression::VerbWriting,
     24,
     {
         OI(Face::FieldIndex::EyeDy, 0),
         OI(Face::FieldIndex::EyeRx, 28),
         OI(Face::FieldIndex::EyeOpenAmt, 25),
         OI(Face::FieldIndex::EyeArcAmt, 24),
         OI(Face::FieldIndex::EyeThick, 3),
         OI(Face::FieldIndex::EyeWaveAmp, 0),
         OI(Face::FieldIndex::EyeWaveFreq, 0),
         OI(Face::FieldIndex::EyeWaveSpeed, 0),
         OI(Face::FieldIndex::PupilDx, 0),
         OI(Face::FieldIndex::PupilDy, -9),
         OI(Face::FieldIndex::PupilR, 15),
         OI(Face::FieldIndex::MouthDy, 0),
         OI(Face::FieldIndex::MouthRx, 19),
         OI(Face::FieldIndex::MouthOpenAmt, 7),
         OI(Face::FieldIndex::MouthArcAmt, 31),
         OI(Face::FieldIndex::MouthThick, 3),
         OI(Face::FieldIndex::MouthWaveAmp, 0),
         OI(Face::FieldIndex::MouthWaveFreq, 0),
         OI(Face::FieldIndex::MouthWaveSpeed, 0),
         OI(Face::FieldIndex::FaceRot, 0),
         OI(Face::FieldIndex::FaceY, -13),
         OI(Face::FieldIndex::RingR, 104),
         OI(Face::FieldIndex::RingG, 118),
         OI(Face::FieldIndex::RingB, 228),
     }},
    {Face::Expression::VerbExecuting,
     24,
     {
         OI(Face::FieldIndex::EyeDy, 0),
         OI(Face::FieldIndex::EyeRx, 30),
         OI(Face::FieldIndex::EyeOpenAmt, 13),
         OI(Face::FieldIndex::EyeArcAmt, 0),
         OI(Face::FieldIndex::EyeThick, 3),
         OI(Face::FieldIndex::EyeWaveAmp, 0),
         OI(Face::FieldIndex::EyeWaveFreq, 0),
         OI(Face::FieldIndex::EyeWaveSpeed, 0),
         OI(Face::FieldIndex::PupilDx, 0),
         OI(Face::FieldIndex::PupilDy, -3),
         OI(Face::FieldIndex::PupilR, 11),
         OI(Face::FieldIndex::MouthDy, 0),
         OI(Face::FieldIndex::MouthRx, 11),
         OI(Face::FieldIndex::MouthOpenAmt, 1),
         OI(Face::FieldIndex::MouthArcAmt, 13),
         OI(Face::FieldIndex::MouthThick, 3),
         OI(Face::FieldIndex::MouthWaveAmp, 0),
         OI(Face::FieldIndex::MouthWaveFreq, 55),
         OI(Face::FieldIndex::MouthWaveSpeed, 0),
         OI(Face::FieldIndex::FaceRot, 0),
         OI(Face::FieldIndex::FaceY, 1),
         OI(Face::FieldIndex::RingR, 156),
         OI(Face::FieldIndex::RingG, 64),
         OI(Face::FieldIndex::RingB, 216),
     }},
    {Face::Expression::VerbStraining,
     24,
     {
         OI(Face::FieldIndex::EyeDy, 1),
         OI(Face::FieldIndex::EyeRx, 30),
         OI(Face::FieldIndex::EyeOpenAmt, 22),
         OI(Face::FieldIndex::EyeArcAmt, 0),
         OI(Face::FieldIndex::EyeThick, 3),
         OI(Face::FieldIndex::EyeWaveAmp, 0),
         OI(Face::FieldIndex::EyeWaveFreq, 3),
         OI(Face::FieldIndex::EyeWaveSpeed, 25),
         OI(Face::FieldIndex::PupilDx, 0),
         OI(Face::FieldIndex::PupilDy, -3),
         OI(Face::FieldIndex::PupilR, 10),
         OI(Face::FieldIndex::MouthDy, 1),
         OI(Face::FieldIndex::MouthRx, 18),
         OI(Face::FieldIndex::MouthOpenAmt, 1),
         OI(Face::FieldIndex::MouthArcAmt, 0),
         OI(Face::FieldIndex::MouthThick, 3),
         OI(Face::FieldIndex::MouthWaveAmp, 4),
         OI(Face::FieldIndex::MouthWaveFreq, 96),
         OI(Face::FieldIndex::MouthWaveSpeed, 364),
         OI(Face::FieldIndex::FaceRot, 0),
         OI(Face::FieldIndex::FaceY, 0),
         OI(Face::FieldIndex::RingR, 210),
         OI(Face::FieldIndex::RingG, 75),
         OI(Face::FieldIndex::RingB, 220),
     }},
    {Face::Expression::VerbSleeping,
     24,
     {
         OI(Face::FieldIndex::EyeDy, 2),
         OI(Face::FieldIndex::EyeRx, 30),
         OI(Face::FieldIndex::EyeOpenAmt, 0),
         OI(Face::FieldIndex::EyeArcAmt, 15),
         OI(Face::FieldIndex::EyeThick, 3),
         OI(Face::FieldIndex::EyeWaveAmp, 0),
         OI(Face::FieldIndex::EyeWaveFreq, 0),
         OI(Face::FieldIndex::EyeWaveSpeed, 1),
         OI(Face::FieldIndex::PupilDx, 0),
         OI(Face::FieldIndex::PupilDy, 3),
         OI(Face::FieldIndex::PupilR, 15),
         OI(Face::FieldIndex::MouthDy, 1),
         OI(Face::FieldIndex::MouthRx, 15),
         OI(Face::FieldIndex::MouthOpenAmt, 0),
         OI(Face::FieldIndex::MouthArcAmt, 0),
         OI(Face::FieldIndex::MouthThick, 3),
         OI(Face::FieldIndex::MouthWaveAmp, 0),
         OI(Face::FieldIndex::MouthWaveFreq, 0),
         OI(Face::FieldIndex::MouthWaveSpeed, 1),
         OI(Face::FieldIndex::FaceRot, 0),
         OI(Face::FieldIndex::FaceY, 17),
         OI(Face::FieldIndex::RingR, 0),
         OI(Face::FieldIndex::RingG, 0),
         OI(Face::FieldIndex::RingB, 0),
     }},
};
#undef OI

static constexpr size_t kVerbTimelineCount = sizeof(kVerbTimelines) / sizeof(kVerbTimelines[0]);
static_assert(kVerbTimelineCount == 6, "Expected 6 verb timeline rows");

// ─── Arm presets per Face::Expression (emotion blend source) ─────────────

struct ArmPreset {
  int16_t min_deg;
  int16_t max_deg;
  float period_s;
  float interval_s;
};

static constexpr ArmPreset kArmPresets[(uint8_t)Face::Expression::Count] = {
    {-25, -15, 2.0f, 1.0f},   // Neutral
    {-23, -7, 1.5f, 0.5f},    // Happy
    {-15, -5, 1.0f, 0.0f},    // Excited
    {10, 25, 0.9f, 0.2f},     // Joyful
    {-25, -15, 2.0f, 1.0f},   // Sad
    {-25, -15, 2.0f, 1.0f},   // VerbThinking
    {-25, -15, 2.0f, 1.0f},   // VerbReading
    {-25, -15, 2.0f, 1.0f},   // VerbWriting
    {-25, -15, 2.0f, 1.0f},   // VerbExecuting
    {-25, -15, 2.0f, 1.0f},   // VerbStraining
    {-25, -15, 2.0f, 1.0f},   // VerbSleeping
    {-25, -15, 2.0f, 1.0f},   // OverlayWaking
    {-25, -15, 2.0f, 1.0f},   // OverlayAttention
    {-25, -20, 3.0f, 6.0f},   // Sleepy
    {-15, -5, 1.0f, 0.0f},    // Distressed
    {-25, -20, 3.0f, 6.0f},   // Blissed
    {-25, -20, 3.0f, 6.0f},   // Depressed
    {-15, -5, 1.0f, 0.0f},    // Shocked
    {-23, -7, 1.5f, 0.5f},    // Disappointed
    {-20, -5, 1.4f, 0.45f},   // Cheeky
    {10, 25, 0.9f, 0.2f},     // Gleeful
    {-18, -8, 1.1f, 0.15f},   // Frustrated
};

// ─── Arm motion choreography (was hal/MotionBehaviors.cpp kMotion[]) ──────

enum class MotionMode : uint8_t {
  None = 0,
  Static,
  RandomDrift,
  Oscillate,
  Waggle,
  Thinking,
};

struct ExprMotionRow {
  MotionMode mode;
  int8_t center;
  uint8_t amplitude;
  uint16_t period_ms;
  uint16_t period_jitter_ms;
  uint16_t slew_ms;
};

static constexpr ExprMotionRow kMotion[(uint8_t)Face::Expression::Count] = {
    /* Neutral */ {MotionMode::RandomDrift, -20, 5, 5000, 5000, 500},
    /* Happy */ {MotionMode::RandomDrift, -15, 8, 2000, 1000, 500},
    /* Excited */ {MotionMode::Oscillate, -10, 5, 1000, 0, 0},
    /* Joyful */ {MotionMode::Waggle, 0, 15, 900, 0, 0},
    /* Sad */ {MotionMode::None, 0, 0, 0, 0, 0},
    /* VerbThinking */ {MotionMode::Thinking, -15, 5, 2000, 0, 0},
    /* VerbReading */ {MotionMode::Static, -8, 0, 0, 0, 0},
    /* VerbWriting */ {MotionMode::Oscillate, 5, 4, 840, 0, 250},
    /* VerbExecuting */ {MotionMode::Oscillate, -5, 5, 1000, 0, 0},
    /* VerbStraining */ {MotionMode::Oscillate, 0, 5, 750, 0, 0},
    /* VerbSleeping */ {MotionMode::Oscillate, -20, 5, 8000, 0, 0},
    /* OverlayWaking */ {MotionMode::Static, 18, 0, 0, 0, 0},
    /* OverlayAttention */ {MotionMode::Waggle, 0, 15, 900, 0, 0},
    /* Sleepy */ {MotionMode::Oscillate, -18, 4, 5000, 0, 0},
    /* Distressed */ {MotionMode::Oscillate, 0, 6, 900, 0, 0},
    /* Blissed */ {MotionMode::RandomDrift, -10, 6, 3000, 1500, 500},
    /* Depressed */ {MotionMode::None, 0, 0, 0, 0, 0},
    /* Shocked */ {MotionMode::Static, 0, 0, 0, 0, 0},
    /* Disappointed */ {MotionMode::None, 0, 0, 0, 0, 0},
    /* Cheeky */ {MotionMode::Waggle, 0, 12, 880, 0, 0},
    /* Gleeful */ {MotionMode::Waggle, 0, 15, 900, 0, 0},
    /* Frustrated */ {MotionMode::Oscillate, 0, 6, 820, 0, 0},
};

// Face vertical bob: follow blended arm waggle heuristic (legacy emotion path).
inline constexpr int16_t kBobAmpFollowEmotionArm = (int16_t)(0x8000);

enum class GazeStyle : uint8_t {
  Off = 0,
  IdleRandom,
  Orbit,
  ScanX,
};

/// Blink / gaze / face-bob policy per expression (`08`). Blended for emotions.
struct IdleAnimRow {
  uint16_t blink_period_min_ms;
  uint16_t blink_period_max_ms;  ///< Inclusive upper bound for random interval.
  uint16_t blink_close_ms;
  uint16_t blink_open_ms;
  int16_t bob_amplitude_px;  ///< `kBobAmpFollowEmotionArm` = use arm-waggle heuristic.
  GazeStyle gaze_style;
  uint16_t gaze_move_ms;
  int16_t gaze_rand_span_x;
  int16_t gaze_rand_span_y;
  uint32_t gaze_reroll_min_ms;
  uint32_t gaze_reroll_max_ms;
  uint32_t gaze_scan_period_ms;
  int16_t gaze_amp_x;
  int16_t gaze_amp_y;
};

static constexpr IdleAnimRow kIdleAnim[(uint8_t)Face::Expression::Count] = {
    /* Neutral */
    {4000, 6499, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::IdleRandom, 200, 15, 10, 1000,
     10000, 0, 0, 0},
    /* Happy */
    {3000, 4499, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::ScanX, 0, 0, 0, 0, 0, 5500, 2, 0},
    /* Excited */
    {2500, 3999, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Orbit, 0, 0, 0, 0, 0, 3500, 3, 2},
    /* Joyful */
    {0, 0, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Sad */
    {0, 0, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* VerbThinking */
    {2000, 3499, 80, 130, 0, GazeStyle::Orbit, 0, 0, 0, 0, 0, 900, 2, 2},
    /* VerbReading */
    {4000, 5999, 80, 130, 0, GazeStyle::ScanX, 0, 0, 0, 0, 0, 1300, 6, 0},
    /* VerbWriting */
    {3500, 5499, 80, 130, 0, GazeStyle::ScanX, 0, 0, 0, 0, 0, 2200, 2, 0},
    /* VerbExecuting */
    {4500, 6999, 80, 130, 5, GazeStyle::ScanX, 0, 0, 0, 0, 0, 2500, 1, 0},
    /* VerbStraining */
    {4500, 6999, 80, 130, 5, GazeStyle::ScanX, 0, 0, 0, 0, 0, 2500, 1, 0},
    /* VerbSleeping */
    {0, 0, 80, 130, 10, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* OverlayWaking */
    {0, 0, 80, 130, 0, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* OverlayAttention */
    {0, 0, 80, 130, 0, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Sleepy */
    {5000, 7999, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Distressed */
    {2000, 3999, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Blissed */
    {3500, 5499, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Depressed */
    {2000, 3999, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Shocked */
    {2000, 3999, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Disappointed */
    {2000, 3999, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Cheeky */
    {2800, 4199, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Gleeful */
    {2200, 3799, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Frustrated */
    {1800, 3199, 80, 130, kBobAmpFollowEmotionArm, GazeStyle::Off, 0, 0, 0, 0, 0, 0, 0, 0},
};

// ─── Runtime simulation tunables (emotion + frame + verb state machines) ──

struct EmotionSimConfig {
  float tau_ms_activation;
  float tau_ms_valence;
  float tau_ms_raw_follow;
  float snap_hysteresis_dist;
  uint32_t snap_hysteresis_hold_ms;
  float dist_sq_tie_eps;
  float baseline_activation;
};

struct FrameAnimConfig {
  float mood_ring_tau_ms;
  float emotion_geometry_smooth_tau_ms;
  uint32_t tick_interval_ms;
  uint32_t tick_interval_stream_ms;
  uint32_t thinking_flip_dur_ms;
  uint32_t thinking_flip_min_ms;
  uint32_t thinking_flip_max_ms;
  uint32_t progress_fade_ms;
  uint32_t effects_fade_ms;
  uint32_t breath_period_ms;
  float breath_eye_amp_px;
  float breath_mouth_scale;
  int16_t emotion_bob_amp_follow_arm;
  uint16_t default_blink_close_ms;
  uint16_t default_blink_open_ms;
  uint16_t default_gaze_move_ms;
  uint32_t invalid_gaze_reroll_fallback_ms;
};

struct VerbSimConfig {
  uint32_t strain_delay_ms;
  uint32_t default_overlay_duration_ms;
};

struct MotionRuntimeConfig {
  uint16_t default_static_slew_ms;
  uint16_t default_drift_slew_ms;
};

static constexpr EmotionSimConfig kEmotionSim = {
    6000.0f,  // tau_ms_activation
    90000.0f, // tau_ms_valence
    50.0f,    // tau_ms_raw_follow
    0.05f,    // snap_hysteresis_dist
    100,      // snap_hysteresis_hold_ms
    1e-7f,    // dist_sq_tie_eps
    0.5f,     // baseline_activation
};

static constexpr FrameAnimConfig kFrameAnim = {
    200.0f, // mood_ring_tau_ms
    250.0f, // emotion_geometry_smooth_tau_ms
    33,     // tick_interval_ms
    16,     // tick_interval_stream_ms
    600,    // thinking_flip_dur_ms
    3000,   // thinking_flip_min_ms
    6000,   // thinking_flip_max_ms
    280,    // progress_fade_ms
    100,    // effects_fade_ms
    4000,   // breath_period_ms
    1.5f,   // breath_eye_amp_px
    0.5f,   // breath_mouth_scale
    3,      // emotion_bob_amp_follow_arm
    80,     // default_blink_close_ms
    130,    // default_blink_open_ms
    200,    // default_gaze_move_ms
    1000,   // invalid_gaze_reroll_fallback_ms
};

static constexpr VerbSimConfig kVerbSim = {
    5000, // strain_delay_ms
    1000, // default_overlay_duration_ms
};

static constexpr MotionRuntimeConfig kMotionRuntime = {
    250, // default_static_slew_ms
    500, // default_drift_slew_ms
};

static_assert(sizeof(kMotion) / sizeof(kMotion[0]) == (uint8_t)Face::Expression::Count,
              "kMotion rows must match Face::Expression::Count");
static_assert(sizeof(kIdleAnim) / sizeof(kIdleAnim[0]) == (uint8_t)Face::Expression::Count,
              "kIdleAnim rows must match Face::Expression::Count");

}  // namespace FaceConfig
