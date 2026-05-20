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

// ─── NamedEmotion → Face::Expression (1:1) ───────────────────────────────

inline Face::Expression expressionForNamedEmotion(EmotionSystem::NamedEmotion e) {
  const uint8_t idx = (uint8_t)e;
  if (idx >= (uint8_t)EmotionSystem::NamedEmotion::Count) return Face::Expression::Neutral;
  return kNamedEmotionToExpression[idx];
}

// ─── Stable strings (debug / bridge / logging) ───────────────────────────

inline const char* emotionName(EmotionSystem::NamedEmotion e) {
  const uint8_t idx = (uint8_t)e;
  if (idx >= (uint8_t)EmotionSystem::NamedEmotion::Count) return "?";
  return kEmotionNames[idx];
}

inline const char* expressionName(Face::Expression e) {
  const uint8_t idx = (uint8_t)e;
  if (idx >= (uint8_t)Face::Expression::Count) return "?";
  return kExpressionNames[idx];
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
    case Face::Expression::VerbAttractingAttention:
      return true;
    case Face::Expression::Neutral:
    case Face::Expression::Happy:
    case Face::Expression::VerbWaking:
    case Face::Expression::VerbSleeping:
    default:
      return false;
  }
}

}  // namespace FaceConfig
