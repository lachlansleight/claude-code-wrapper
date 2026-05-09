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

// ─── Per-expression geometry (FaceParams) ─────────────────────────────────

// Field order matches Face::FaceParams in SceneTypes.h.
static const Face::FaceParams kBaseTargets[(uint8_t)Face::Expression::Count] = {
    /* Neutral */          {  2, 30,  -26, 0, +26, 0, 3,  0, 0, 0,   0,  3, 15,
                              0, 15,   +2, 0,  +2, 0, 3,  0, 0, 0,
                              0, 0,    0, 0, 0 },
    /* Happy */            {  0, 30,  -16, 0, +30, 0, 3,  0, 0, 0,   0,  5,  16,
                              0,  +24,   3, 0,  3, 0, 3,  0, 0, 0,
                              0, 5,    0, 0, 0 },
    /* Excited */          {  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   0,  0,  17,
                              0,  +27,   4, -2,  8, -2, 3,  0, 0, 0,
                              0, 0,    40, 255, 80 },
    /* Joyful */           { -5, 20,  -15, 0, -6, 0, 4,  0, 0, 0,   0,  0,  14,
                              -11,  +37,   3, 0,  24, 0, 4,  0, 0, 0,
                              0, -14,    255, 228, 38 },
    /* Sad */              {  4, 28,  -12, 0, +17, 0, 3,  0, 0, 0,   0,  3,  11,
                              4,  +20,   -13, -7,  -11, -8, 3,  0, 0, 0,
                              0, 6,    0, 0, 0 },
    /* VerbThinking */     {  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   7, -9, 15,
                              0, 11,   +3, 0,  +3, 0, 3,  0, 0, 0,
                            -10, 0,    36, 56, 120 },
    /* VerbReading */      {  0, 28,  -26, 0, +26, 0, 3,  0, 0, 0,   0,  8, 12,
                              0,  9,   +3, 0,  +3, 0, 3,  0, 0, 0,
                              0, 12,   78, 146, 210 },
    /* VerbWriting */      {  0, 30,  -26, 0, +26, 0, 3,  0, 0, 0,   0, -8, 15,
                              0, 15,    0, 0, +14, 0, 3,  0, 0, 0,
                              0, 0,    104, 118, 228 },
    /* VerbExecuting */    {  0, 30,  -16, 0, +16, 0, 3,  0, 0, 0,   0, -4, 10,
                              0,  9,   +2, 0,  +2, 0, 3,  0, 0, 0,
                              0, 0,    156, 64, 216 },
    /* VerbStraining */    {  0, 30,  -22, 0, +22, 0, 3,  0, 0, 0,   0, -3, 10,
                              0, 18,    0, 0,   0, 0, 3,  4, 100, 360,
                              0, 0,    210, 75, 220 },
    /* VerbSleeping */     {  8, 26,   -2, 0,  +2, 0, 3,  0, 0, 0,   0,  0,  15,
                              0,  9,    0, 0,   0, 0, 3,  0, 0, 0,
                              0, 0,    0, 0, 0 },
    /* OverlayWaking */    { -2, 34,  -34, 0, +34, 0, 3,  0, 0, 0,   0,  0, 18,
                              0,  7,   -9, 0,  +9, 0, 3,  0, 0, 0,
                              0, 0,    128, 128, 128 },
    /* OverlayAttention */ { -2, 34,  -34, 0, +34, 0, 3,  0, 0, 0,   0,  0, 18,
                              0,  7,   -9, 0,  +9, 0, 3,  0, 0, 0,
                              0, 0,    255, 20, 40 },
    /* Sleepy */           {  0, 28,  0, 10, +34, 10, 3,  0, 0, 0,   0,  0,  15,
                              0,  +13,   0, 0,  3, 0, 3,  0, 17, 90,
                              0, 9,    0, 0, 0 },
    /* Distressed */       {  2, 30,  -26, 0, +33, 0, 3,  0, 0, 0,   0,  7,  10,
                              4,  +24,   -19, -7,  -7, 0, 3,  0, 0, 0,
                              0, -15,    255, 48, 24 },
    /* Blissed */          {  1, 20,   +3, 0, +1, 0, 3,  0, 0, 0,   0,  0,  15,
                              1,  +26,   3, 0,  13, 0, 3,  0, 0, 0,
                              0, 5,    0, 0, 0 },
    /* Depressed */        {  0, 30,   +16, 10, +34, 11, 3,  0, 0, 0,   0,  20,  6,
                              0,  +13,   0, +6,  3, 4, 3,  0, 17, 90,
                              0, 9,    0, 0, 0 },
    /* Shocked */          {  0, 30,   -34, 0,   39, 0, 3,   1, 85, 720,   0, 3, 9,
                             20, 17,  -17, 0,   8, 0, 1,    2, 49, 720,
                              0, 0,     255, 255, 255 },
    /* Disappointed */     {  3, 21,   +6, 0, +6, 0, 3,  0, 0, 0,   0,  3,  8,
                              5,  +26,   -1, 0,  -3, 0, 3,  0, 0, 0,
                              0, 0,    229, 54, 95 },
    /* Cheeky */           {  1, 30,  -31, 0, +8, 0, 3,  0, 0, 0,   0,  3,  15,
                              -25,  +15,   11, 0,  8, 0, 3,  0, 0, 0,
                              0, -3,    0, 0, 0 },
    /* Gleeful */          {  1, 27,  -30, 0, -2, 0, 3,  0, 0, 0,   0,  -7,  10,
                              -25,  +27,   0, -2,  20, -2, 3,  0, 0, 0,
                              0, 5,    39, 248, 78 },
    /* Frustrated */       {  0, 30,  -22, 0, +22, 0, 3,  0, 0, 0,   0, -3, 10,
                              0, 18,    0, 0,   0, 0, 3,  4, 100, 360,
                              0, 0,    210, 75, 220 },
};

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
