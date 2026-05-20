#include "SceneContextFill.h"

#include <string.h>

#include "../agents/AgentEvents.h"
#include "../behaviour/EmotionBlend.h"
#include "../behaviour/EmotionSystem.h"
#include "../face/FACE_CONFIG.h"
#include "../behaviour/VerbSystem.h"
#include "../core/AsciiCopy.h"
#include "../hal/Settings.h"

namespace SceneContextFill {

namespace {

void copyField(char* dst, size_t cap, const char* src) {
  AsciiCopy::copy(dst, cap, src ? src : "");
}

void copyBody(char* dst, size_t cap, const char* src) {
  AsciiCopy::copyPreserveNewlines(dst, cap, src ? src : "");
}

Face::Expression expressionForEmotion(EmotionSystem::NamedEmotion e) {
  return FaceConfig::expressionForNamedEmotion(e);
}

Face::Expression expressionForVerb(VerbSystem::Verb v) {
  const uint8_t i = (uint8_t)v;
  if (i >= (uint8_t)VerbSystem::Verb::Count) return Face::Expression::Neutral;
  return FaceConfig::kVerbToExpression[i];
}

uint8_t clampRingChannel(int16_t v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

void accentFromRing(const Face::FaceParams& p, uint8_t& r, uint8_t& g, uint8_t& b) {
  r = clampRingChannel(p.ring_r.value);
  g = clampRingChannel(p.ring_g.value);
  b = clampRingChannel(p.ring_b.value);
}

}  // namespace

void fill(Face::SceneContext& out) {
  memset(&out, 0, sizeof(out));

  const AgentEvents::AgentState& st = AgentEvents::state();
  copyField(out.latched_session, sizeof(out.latched_session), st.latched_session);
  copyField(out.pending_permission, sizeof(out.pending_permission), st.pending_permission);
  copyField(out.status_line, sizeof(out.status_line), st.status_line);
  copyBody(out.body_text, sizeof(out.body_text), st.body_text);
  copyField(out.subtitle_tool, sizeof(out.subtitle_tool), st.subtitle_tool);
  out.thinking_title_since_ms = st.thinking_title_since_ms;
  copyField(out.latest_shell_command, sizeof(out.latest_shell_command), st.latest_shell_command);
  copyField(out.latest_read_target, sizeof(out.latest_read_target), st.latest_read_target);
  copyField(out.latest_write_target, sizeof(out.latest_write_target), st.latest_write_target);
  out.turn_started_wall_ms = st.turn_started_wall_ms;
  out.done_turn_elapsed_ms = st.done_turn_elapsed_ms;

  out.read_tools_this_turn = st.read_tools_this_turn;
  out.write_tools_this_turn = st.write_tools_this_turn;
  out.ws_connected = st.ws_connected;
  out.render_mode = (uint8_t)AgentEvents::renderMode();
  out.face_mode = (AgentEvents::renderMode() == AgentEvents::RENDER_FACE);
  out.settings_version = Settings::settingsVersion();

  const Settings::Rgb888 fg = Settings::colorRgb(Settings::NamedColor::Foreground);
  const Settings::Rgb888 bg = Settings::colorRgb(Settings::NamedColor::Background);
  out.fg_r = fg.r;
  out.fg_g = fg.g;
  out.fg_b = fg.b;
  out.bg_r = bg.r;
  out.bg_g = bg.g;
  out.bg_b = bg.b;

  const EmotionSystem::Emotion raw = EmotionSystem::raw();
  out.mood_v = raw.valence;
  out.mood_a = raw.activation;
  out.base_face_params = EmotionBlend::blendedFaceParams(raw.valence, raw.activation);

  const EmotionSystem::DebugState emotionDebug = EmotionSystem::debugState();
  copyField(out.snapped_emotion, sizeof(out.snapped_emotion),
            EmotionSystem::emotionName(emotionDebug.snappedCurrent));
  copyField(out.pending_snapped_emotion, sizeof(out.pending_snapped_emotion),
            EmotionSystem::emotionName(emotionDebug.snappedPending));
  out.pending_snap_active = emotionDebug.pendingSnapActive;
  out.pending_snap_since_ms = emotionDebug.pendingSnapSinceMs;
  out.held_driver_count = emotionDebug.heldDriverCount;
  for (uint8_t i = 0; i < emotionDebug.heldDriverCount && i < 8; ++i) {
    out.held_driver_ids[i] = emotionDebug.heldDrivers[i].id;
    out.held_driver_targets[i] = emotionDebug.heldDrivers[i].targetValence;
  }

  const VerbSystem::DebugState verbDebug = VerbSystem::debugState();
  copyField(out.verb_current, sizeof(out.verb_current), VerbSystem::verbName(verbDebug.current));
  copyField(out.verb_effective, sizeof(out.verb_effective), VerbSystem::verbName(verbDebug.effective));
  out.verb_overlay_active = verbDebug.overlayActive;
  out.verb_overlay_queued = verbDebug.overlayQueued;
  out.verb_time_in_current_ms = VerbSystem::timeInEffectiveMs();
  const uint32_t now = millis();
  out.verb_linger_remaining_ms =
      (verbDebug.lingerUntilMs > now) ? (verbDebug.lingerUntilMs - now) : 0;
  out.verb_overlay_remaining_ms =
      (verbDebug.overlayUntilMs > now) ? (verbDebug.overlayUntilMs - now) : 0;

  const VerbSystem::Verb eff = VerbSystem::effective();
  if (eff != VerbSystem::Verb::None) {
    out.effective_expression = expressionForVerb(eff);
    out.expression_entered_at_ms = VerbSystem::effectiveEnteredAtMs();
  } else {
    out.effective_expression = expressionForEmotion(EmotionSystem::snapped().named);
    out.expression_entered_at_ms = 0;
  }

  if (eff != VerbSystem::Verb::None) {
    const uint8_t idx = (uint8_t)out.effective_expression;
    const Face::FaceParams& target =
        (idx < (uint8_t)Face::Expression::Count) ? FaceConfig::kBaseTargets[idx]
                                                   : FaceConfig::kBaseTargets[0];
    accentFromRing(target, out.accent_r, out.accent_g, out.accent_b);
  } else {
    accentFromRing(out.base_face_params, out.accent_r, out.accent_g, out.accent_b);
  }
}

}  // namespace SceneContextFill
