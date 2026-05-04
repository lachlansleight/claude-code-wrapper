#include "SceneContextFill.h"

#include <string.h>

#include "../agents/AgentEvents.h"
#include "../behaviour/EmotionSystem.h"
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
  switch (e) {
    case EmotionSystem::NamedEmotion::Happy:
      return Face::Expression::Happy;
    case EmotionSystem::NamedEmotion::Excited:
      return Face::Expression::Excited;
    case EmotionSystem::NamedEmotion::Joyful:
      return Face::Expression::Joyful;
    case EmotionSystem::NamedEmotion::Sad:
      return Face::Expression::Sad;
    case EmotionSystem::NamedEmotion::Neutral:
    default:
      return Face::Expression::Neutral;
  }
}

Face::Expression expressionForVerb(VerbSystem::Verb v) {
  switch (v) {
    case VerbSystem::Verb::Thinking:
      return Face::Expression::VerbThinking;
    case VerbSystem::Verb::Reading:
      return Face::Expression::VerbReading;
    case VerbSystem::Verb::Writing:
      return Face::Expression::VerbWriting;
    case VerbSystem::Verb::Executing:
      return Face::Expression::VerbExecuting;
    case VerbSystem::Verb::Straining:
      return Face::Expression::VerbStraining;
    case VerbSystem::Verb::Sleeping:
      return Face::Expression::VerbSleeping;
    case VerbSystem::Verb::Waking:
      return Face::Expression::OverlayWaking;
    case VerbSystem::Verb::AttractingAttention:
      return Face::Expression::OverlayAttention;
    case VerbSystem::Verb::None:
    default:
      return Face::Expression::Neutral;
  }
}

Settings::NamedColor accentNamedColor(Face::Expression e) {
  switch (e) {
    case Face::Expression::Neutral:
      return Settings::NamedColor::Background;
    case Face::Expression::Happy:
      return Settings::NamedColor::Happy;
    case Face::Expression::Excited:
      return Settings::NamedColor::Excited;
    case Face::Expression::Joyful:
      return Settings::NamedColor::Joyful;
    case Face::Expression::Sad:
      return Settings::NamedColor::Sad;
    case Face::Expression::VerbThinking:
      return Settings::NamedColor::Thinking;
    case Face::Expression::VerbReading:
      return Settings::NamedColor::Reading;
    case Face::Expression::VerbWriting:
      return Settings::NamedColor::Writing;
    case Face::Expression::VerbExecuting:
      return Settings::NamedColor::Executing;
    case Face::Expression::VerbStraining:
      return Settings::NamedColor::Straining;
    case Face::Expression::VerbSleeping:
      return Settings::NamedColor::Sleeping;
    case Face::Expression::OverlayWaking:
      return Settings::NamedColor::Excited;
    case Face::Expression::OverlayAttention:
      return Settings::NamedColor::Attention;
    default:
      return Settings::NamedColor::Foreground;
  }
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

  const VerbSystem::Verb eff = VerbSystem::effective();
  if (VerbSystem::overlayActive()) {
    out.effective_expression = expressionForVerb(eff);
    out.expression_entered_at_ms = VerbSystem::enteredAtMs();
  } else if (eff != VerbSystem::Verb::None) {
    out.effective_expression = expressionForVerb(eff);
    out.expression_entered_at_ms = VerbSystem::enteredAtMs();
  } else {
    out.effective_expression = expressionForEmotion(EmotionSystem::snapped().named);
    out.expression_entered_at_ms = 0;
  }

  const Settings::Rgb888 ac = Settings::colorRgb(accentNamedColor(out.effective_expression));
  out.accent_r = ac.r;
  out.accent_g = ac.g;
  out.accent_b = ac.b;
}

}  // namespace SceneContextFill
