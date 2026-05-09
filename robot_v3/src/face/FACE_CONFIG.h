#pragma once

/**
 * @file FACE_CONFIG.h
 * @brief Face config helpers; numeric tables live in `FACE_CONFIG_DATA.h`.
 *
 * The editor should eventually emit `FACE_CONFIG_DATA.h` only. This header
 * stays hand-maintained (mapping switches, string tables, policy).
 */

#include "FACE_CONFIG_DATA.h"

namespace FaceConfig {

inline ArmPreset armPresetFor(Face::Expression e) {
  const uint8_t i = (uint8_t)e;
  if (i >= (uint8_t)Face::Expression::Count) return kArmPresets[0];
  return kArmPresets[i];
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
