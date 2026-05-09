#pragma once

#include <Arduino.h>

/**
 * @file FaceEnums.h
 * @brief Face::Expression + EmotionSystem::NamedEmotion (shared contract).
 *
 * Included from SceneTypes.h and FACE_CONFIG.h. Editor Phase 1 will merge
 * these into generated FACE_CONFIG.h; for now they stay in a tiny header so
 * SceneTypes stays free of behaviour tables.
 */

namespace Face {

/**
 * Effective expression from composition (verb, emotion, overlay).
 * Order is the contract for kBaseTargets, kMotion, etc.
 */
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

/// True for expressions driven by the continuous (v, a) emotion layer.
inline bool isEmotionExpression(Expression s) {
  return s == Expression::Neutral || s == Expression::Happy || s == Expression::Excited ||
         s == Expression::Joyful || s == Expression::Sad || s == Expression::Sleepy ||
         s == Expression::Distressed || s == Expression::Blissed || s == Expression::Depressed ||
         s == Expression::Shocked || s == Expression::Disappointed || s == Expression::Cheeky ||
         s == Expression::Gleeful || s == Expression::Frustrated;
}

}  // namespace Face

namespace EmotionSystem {

/// Named emotion regions (V/A anchors). Order matches kEmotionPoints in FACE_CONFIG_DATA.h.
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
