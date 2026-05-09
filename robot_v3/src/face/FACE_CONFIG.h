#pragma once

/**
 * @file FACE_CONFIG.h
 * @brief Single source for face preset tables (Phase 1 PR A).
 *
 * See EDITOR_BRIEF/. Row order for kBaseTargets matches Face::Expression.
 * kEmotionPoints order matches EmotionSystem::NamedEmotion.
 */

#include "SceneTypes.h"

namespace FaceConfig {

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

// ─── Per-expression geometry (ParamI16, PR B) ─────────────────────────────
// Emotion rows: full strength presets for Delaunay blend. Verb / overlay rows
// are zeroed — face geometry comes from VerbTimeline.cpp + EffectsRenderer.
#ifndef FACE_P
#define FACE_P(V) ::Face::ParamI16{ (int16_t)(V), 100 }
#endif
static const Face::FaceParams kBaseTargets[(uint8_t)Face::Expression::Count] = {
    /* Neutral */
    {FACE_P(2), FACE_P(30), FACE_P(-26), FACE_P(0), FACE_P(26), FACE_P(0), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(3), FACE_P(15), FACE_P(0), FACE_P(15), FACE_P(2),
     FACE_P(0), FACE_P(2), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Happy */
    {FACE_P(0), FACE_P(30), FACE_P(-16), FACE_P(0), FACE_P(30), FACE_P(0), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(5), FACE_P(16), FACE_P(0), FACE_P(24), FACE_P(3),
     FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0),
     FACE_P(5), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Excited */
    {FACE_P(0), FACE_P(30), FACE_P(-30), FACE_P(0), FACE_P(30), FACE_P(0), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(17), FACE_P(0), FACE_P(27), FACE_P(4),
     FACE_P(-2), FACE_P(8), FACE_P(-2), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(40), FACE_P(255), FACE_P(80)},
    /* Joyful */
    {FACE_P(-5), FACE_P(20), FACE_P(-15), FACE_P(0), FACE_P(-6), FACE_P(0), FACE_P(4), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(14), FACE_P(-11), FACE_P(37), FACE_P(3),
     FACE_P(0), FACE_P(24), FACE_P(0), FACE_P(4), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(-14), FACE_P(255), FACE_P(228), FACE_P(38)},
    /* Sad */
    {FACE_P(4), FACE_P(28), FACE_P(-12), FACE_P(0), FACE_P(17), FACE_P(0), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(3), FACE_P(11), FACE_P(4), FACE_P(20), FACE_P(-13),
     FACE_P(-7), FACE_P(-11), FACE_P(-8), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(6), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* VerbThinking */ Face::FaceParams{},
    /* VerbReading */ Face::FaceParams{},
    /* VerbWriting */ Face::FaceParams{},
    /* VerbExecuting */ Face::FaceParams{},
    /* VerbStraining */ Face::FaceParams{},
    /* VerbSleeping */ Face::FaceParams{},
    /* OverlayWaking */ Face::FaceParams{},
    /* OverlayAttention */ Face::FaceParams{},
    /* Sleepy */
    {FACE_P(0), FACE_P(28), FACE_P(0), FACE_P(10), FACE_P(34), FACE_P(10), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(15), FACE_P(0), FACE_P(13), FACE_P(0),
     FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(17), FACE_P(90), FACE_P(0),
     FACE_P(9), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Distressed */
    {FACE_P(2), FACE_P(30), FACE_P(-26), FACE_P(0), FACE_P(33), FACE_P(0), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(7), FACE_P(10), FACE_P(4), FACE_P(24), FACE_P(-19),
     FACE_P(-7), FACE_P(-7), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(-15), FACE_P(255), FACE_P(48), FACE_P(24)},
    /* Blissed */
    {FACE_P(1), FACE_P(20), FACE_P(3), FACE_P(0), FACE_P(1), FACE_P(0), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(15), FACE_P(1), FACE_P(26), FACE_P(3),
     FACE_P(0), FACE_P(13), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(5), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Depressed */
    {FACE_P(0), FACE_P(30), FACE_P(16), FACE_P(10), FACE_P(34), FACE_P(11), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(20), FACE_P(6), FACE_P(0), FACE_P(13), FACE_P(0),
     FACE_P(6), FACE_P(3), FACE_P(4), FACE_P(3), FACE_P(0), FACE_P(17), FACE_P(90), FACE_P(0),
     FACE_P(9), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Shocked */
    {FACE_P(0), FACE_P(30), FACE_P(-34), FACE_P(0), FACE_P(39), FACE_P(0), FACE_P(3), FACE_P(1),
     FACE_P(85), FACE_P(720), FACE_P(0), FACE_P(3), FACE_P(9), FACE_P(20), FACE_P(17), FACE_P(-17),
     FACE_P(0), FACE_P(8), FACE_P(0), FACE_P(1), FACE_P(2), FACE_P(49), FACE_P(720), FACE_P(0),
     FACE_P(0), FACE_P(255), FACE_P(255), FACE_P(255)},
    /* Disappointed */
    {FACE_P(3), FACE_P(21), FACE_P(6), FACE_P(0), FACE_P(6), FACE_P(0), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(3), FACE_P(8), FACE_P(5), FACE_P(26), FACE_P(-1),
     FACE_P(0), FACE_P(-3), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(0), FACE_P(229), FACE_P(54), FACE_P(95)},
    /* Cheeky */
    {FACE_P(1), FACE_P(30), FACE_P(-31), FACE_P(0), FACE_P(8), FACE_P(0), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(3), FACE_P(15), FACE_P(-25), FACE_P(15), FACE_P(11),
     FACE_P(0), FACE_P(8), FACE_P(0), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(-3), FACE_P(0), FACE_P(0), FACE_P(0)},
    /* Gleeful */
    {FACE_P(1), FACE_P(27), FACE_P(-30), FACE_P(0), FACE_P(-2), FACE_P(0), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(-7), FACE_P(10), FACE_P(-25), FACE_P(27), FACE_P(0),
     FACE_P(-2), FACE_P(20), FACE_P(-2), FACE_P(3), FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(0),
     FACE_P(5), FACE_P(39), FACE_P(248), FACE_P(78)},
    /* Frustrated */
    {FACE_P(0), FACE_P(30), FACE_P(-22), FACE_P(0), FACE_P(22), FACE_P(0), FACE_P(3), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(-3), FACE_P(10), FACE_P(0), FACE_P(18), FACE_P(0),
     FACE_P(0), FACE_P(0), FACE_P(0), FACE_P(3), FACE_P(4), FACE_P(100), FACE_P(360), FACE_P(0),
     FACE_P(0), FACE_P(210), FACE_P(75), FACE_P(220)},
};
#undef FACE_P

// ─── Arm presets per Face::Expression (emotion blend source) ─────────────

struct ArmPreset {
  int16_t min_deg;
  int16_t max_deg;
  float period_s;
  float interval_s;
};

inline ArmPreset armPresetFor(Face::Expression e) {
  static constexpr ArmPreset kByExpr[(uint8_t)Face::Expression::Count] = {
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
  const uint8_t i = (uint8_t)e;
  if (i >= (uint8_t)Face::Expression::Count) return kByExpr[0];
  return kByExpr[i];
}

// ─── NamedEmotion → Face::Expression (1:1) ───────────────────────────────

inline Face::Expression expressionForNamedEmotion(EmotionSystem::NamedEmotion e) {
  switch (e) {
    case EmotionSystem::NamedEmotion::Happy:
      return Face::Expression::Happy;
    case EmotionSystem::NamedEmotion::Excited:
      return Face::Expression::Excited;
    case EmotionSystem::NamedEmotion::Joyful:
      return Face::Expression::Joyful;
    case EmotionSystem::NamedEmotion::Sad:
      return Face::Expression::Sad;
    case EmotionSystem::NamedEmotion::Sleepy:
      return Face::Expression::Sleepy;
    case EmotionSystem::NamedEmotion::Distressed:
      return Face::Expression::Distressed;
    case EmotionSystem::NamedEmotion::Blissed:
      return Face::Expression::Blissed;
    case EmotionSystem::NamedEmotion::Depressed:
      return Face::Expression::Depressed;
    case EmotionSystem::NamedEmotion::Shocked:
      return Face::Expression::Shocked;
    case EmotionSystem::NamedEmotion::Disappointed:
      return Face::Expression::Disappointed;
    case EmotionSystem::NamedEmotion::Cheeky:
      return Face::Expression::Cheeky;
    case EmotionSystem::NamedEmotion::Gleeful:
      return Face::Expression::Gleeful;
    case EmotionSystem::NamedEmotion::Frustrated:
      return Face::Expression::Frustrated;
    case EmotionSystem::NamedEmotion::Neutral:
    default:
      return Face::Expression::Neutral;
  }
}

// ─── Stable strings (debug / bridge / logging) ───────────────────────────

inline const char* emotionName(EmotionSystem::NamedEmotion e) {
  switch (e) {
    case EmotionSystem::NamedEmotion::Neutral:
      return "neutral";
    case EmotionSystem::NamedEmotion::Happy:
      return "happy";
    case EmotionSystem::NamedEmotion::Excited:
      return "excited";
    case EmotionSystem::NamedEmotion::Joyful:
      return "joyful";
    case EmotionSystem::NamedEmotion::Sad:
      return "sad";
    case EmotionSystem::NamedEmotion::Sleepy:
      return "sleepy";
    case EmotionSystem::NamedEmotion::Distressed:
      return "distressed";
    case EmotionSystem::NamedEmotion::Blissed:
      return "blissed";
    case EmotionSystem::NamedEmotion::Depressed:
      return "depressed";
    case EmotionSystem::NamedEmotion::Shocked:
      return "shocked";
    case EmotionSystem::NamedEmotion::Disappointed:
      return "disappointed";
    case EmotionSystem::NamedEmotion::Cheeky:
      return "cheeky";
    case EmotionSystem::NamedEmotion::Gleeful:
      return "gleeful";
    case EmotionSystem::NamedEmotion::Frustrated:
      return "frustrated";
    default:
      return "?";
  }
}

inline const char* expressionName(Face::Expression e) {
  switch (e) {
    case Face::Expression::Neutral:
      return "neutral";
    case Face::Expression::Happy:
      return "happy";
    case Face::Expression::Excited:
      return "excited";
    case Face::Expression::Joyful:
      return "joyful";
    case Face::Expression::Sad:
      return "sad";
    case Face::Expression::VerbThinking:
      return "verb_thinking";
    case Face::Expression::VerbReading:
      return "verb_reading";
    case Face::Expression::VerbWriting:
      return "verb_writing";
    case Face::Expression::VerbExecuting:
      return "verb_executing";
    case Face::Expression::VerbStraining:
      return "verb_straining";
    case Face::Expression::VerbSleeping:
      return "verb_sleeping";
    case Face::Expression::OverlayWaking:
      return "overlay_waking";
    case Face::Expression::OverlayAttention:
      return "overlay_attention";
    case Face::Expression::Sleepy:
      return "sleepy";
    case Face::Expression::Distressed:
      return "distressed";
    case Face::Expression::Blissed:
      return "blissed";
    case Face::Expression::Depressed:
      return "depressed";
    case Face::Expression::Shocked:
      return "shocked";
    case Face::Expression::Disappointed:
      return "disappointed";
    case Face::Expression::Cheeky:
      return "cheeky";
    case Face::Expression::Gleeful:
      return "gleeful";
    case Face::Expression::Frustrated:
      return "frustrated";
    default:
      return "?";
  }
}

/// Mood ring for non-emotion expressions (emotions always draw from blend).
inline bool moodRingEnabledVerbOrOverlay(Face::Expression expr) {
  switch (expr) {
    case Face::Expression::VerbThinking:
    case Face::Expression::VerbReading:
    case Face::Expression::VerbWriting:
    case Face::Expression::VerbExecuting:
    case Face::Expression::VerbStraining:
    case Face::Expression::Joyful:
    case Face::Expression::Excited:
    case Face::Expression::Sad:
    case Face::Expression::Distressed:
    case Face::Expression::Depressed:
    case Face::Expression::Shocked:
    case Face::Expression::Disappointed:
    case Face::Expression::OverlayAttention:
      return true;
    case Face::Expression::Neutral:
    case Face::Expression::Happy:
    case Face::Expression::OverlayWaking:
    case Face::Expression::VerbSleeping:
    default:
      return false;
  }
}

}  // namespace FaceConfig
