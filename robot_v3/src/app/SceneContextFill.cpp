#include "SceneContextFill.h"

#include <string.h>

#include "../agents/AgentEvents.h"
#include "../behaviour/EmotionSystem.h"
#include "../behaviour/VerbSystem.h"
#include "../core/AsciiCopy.h"

namespace SceneContextFill {

namespace {

void copyField(char* dst, size_t cap, const char* src) {
  AsciiCopy::copy(dst, cap, src ? src : "");
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

}  // namespace

void fill(Face::SceneContext& out) {
  memset(&out, 0, sizeof(out));

  const AgentEvents::AgentState& st = AgentEvents::state();
  copyField(out.latched_session, sizeof(out.latched_session), st.latched_session);
  copyField(out.pending_permission, sizeof(out.pending_permission), st.pending_permission);
  copyField(out.status_line, sizeof(out.status_line), st.status_line);
  out.read_tools_this_turn = st.read_tools_this_turn;
  out.write_tools_this_turn = st.write_tools_this_turn;
  out.ws_connected = st.ws_connected;

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
}

}  // namespace SceneContextFill
