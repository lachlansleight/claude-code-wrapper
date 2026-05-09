#pragma once

/**
 * @file FACE_CONFIG_DATA.h
 * @brief Editor-exportable face data only — tables, no helpers.
 *
 * Hand-maintained until the Phase 2 editor emits this file. Firmware helpers
 * live in `FACE_CONFIG.h`. Python `gen_emotion_triangulation.py` parses
 * `kEmotionPoints` from here.
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

// ─── Per-expression geometry (ParamI16) ───────────────────────────────────
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

}  // namespace FaceConfig
