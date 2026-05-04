#pragma once

// Data-only types for the face pipeline. No TFT / bridge / behaviour includes.
// `SceneContext` is filled by the composition layer each frame (or on a debug cadence).

#include <Arduino.h>

namespace Face {

// Single dispatch key for motion + face once the full stack lands.
// Part 2: populated for logging and future FrameController wiring.
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
  Count
};

struct SceneContext {
  Expression effective_expression;
  uint32_t expression_entered_at_ms;

  float mood_v;
  float mood_a;

  char latched_session[40];
  char pending_permission[48];
  char status_line[80];

  uint16_t read_tools_this_turn;
  uint16_t write_tools_this_turn;

  bool ws_connected;
};

const char* expressionName(Expression e);

}  // namespace Face
