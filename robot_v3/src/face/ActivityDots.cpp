#include "ActivityDots.h"

#include <math.h>

namespace Face {

static constexpr int16_t kProgressDotRadiusMin = 5;
static constexpr int16_t kProgressDotRadiusMax = 2;
static constexpr float kProgressArcDegMin = 40.0f;
static constexpr float kProgressArcDegMax = 170.0f;
static constexpr uint16_t kProgressMaxDots = 48;
static constexpr uint32_t kProgressFadeMs = 280;

static void drawProgressDots(TFT_eSprite& s, uint16_t count, float baseRad, float scale, float moodR,
                             float moodG, float moodB) {
  if (count == 0) return;
  if (count > kProgressMaxDots) count = kProgressMaxDots;
  if (scale <= 0.01f) return;
  const uint16_t dotColor = rgb888To565((uint8_t)moodR, (uint8_t)moodG, (uint8_t)moodB);

  float countT = (float)count / (float)kProgressMaxDots;
  if (countT > 1.0f) countT = 1.0f;
  for (uint16_t i = 0; i < count; ++i) {
    const float t = ((float)i + 0.5f) / (float)count;
    float argDeg = kProgressArcDegMin + (kProgressArcDegMax - kProgressArcDegMin) * countT;
    const float spanRad = argDeg * (float)PI / 180.0f;
    const float a = baseRad + (t - 0.5f) * spanRad;

    float currentRadius =
        kProgressDotRadiusMin + (kProgressDotRadiusMax - kProgressDotRadiusMin) * countT;
    int16_t r = (int16_t)(currentRadius * scale);
    if (r < 1) r = 1;

    float arcRadius = (int16_t)(106.0f - currentRadius);
    const int16_t x = kCx + (int16_t)(cosf(a) * arcRadius);
    const int16_t y = kCy + (int16_t)(sinf(a) * arcRadius);

    s.fillCircle(x, y, r, dotColor);
  }
}

void drawActivityDots(TFT_eSprite& s, const SceneRenderState& renderState, const SceneContext& ctx,
                      uint32_t now) {
  if (renderState.expression == Expression::VerbSleeping) return;

  if (renderState.progress_fade_start_ms != 0) {
    const float t =
        (float)(now - renderState.progress_fade_start_ms) / (float)kProgressFadeMs;
    const float scale = 1.0f - smoothstep01(t);
    drawProgressDots(s, renderState.fade_read_count, (float)PI / 2.0f, scale, renderState.mood_r,
                     renderState.mood_g, renderState.mood_b);
    drawProgressDots(s, renderState.fade_write_count, -(float)PI / 2.0f, scale, renderState.mood_r,
                     renderState.mood_g, renderState.mood_b);
    return;
  }

  if (renderState.expression == Expression::Neutral) return;
  drawProgressDots(s, ctx.read_tools_this_turn, (float)PI / 2.0f, 1.0f, renderState.mood_r,
                   renderState.mood_g, renderState.mood_b);
  drawProgressDots(s, ctx.write_tools_this_turn, -(float)PI / 2.0f, 1.0f, renderState.mood_r,
                   renderState.mood_g, renderState.mood_b);
}

}  // namespace Face
