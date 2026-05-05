#include "EventRouter.h"

#include <string.h>

#include "../agents/AgentEvents.h"
#include "../agents/BridgeControl.h"
#include "../behaviour/EmotionSystem.h"
#include "../behaviour/VerbSystem.h"
#include "../bridge/BridgeClient.h"
#include "../core/DebugLog.h"
#include "../hal/Motion.h"
#include "../hal/Settings.h"

namespace EventRouter {

namespace {

static constexpr uint32_t kVerbLingerMs = 1000;
static constexpr uint32_t kOverlayMs = 1000;
static constexpr uint32_t kStrainStressDelayMs = 30000;
static constexpr float kBlockedV = -0.6f;
static constexpr float kStrainV = -0.4f;

bool sPendingPermissionHeld = false;
bool sStrainHeld = false;
uint32_t sLastSessionPollMs = 0;
static constexpr uint32_t kSessionPollMs = 5000;

const char* getCommandAction(JsonDocument& doc) {
  const char* type = doc["type"] | "";
  if (strcmp(type, "emotion.command") == 0) return doc["action"] | "";
  return type;
}

JsonVariantConst getCommandParams(JsonDocument& doc) {
  const char* type = doc["type"] | "";
  if (strcmp(type, "emotion.command") == 0) return doc["params"];
  return doc.as<JsonVariantConst>();
}

void setVerbFromActivity(const AgentEvents::Event& e) {
  if (e.activity_kind && !strcmp(e.activity_kind, "shell.exec")) {
    VerbSystem::setVerb(VerbSystem::Verb::Executing);
    return;
  }
  if (AgentEvents::classifyActivity(e.activity_kind, e.activity_tool, e.activity_summary) ==
      AgentEvents::ACTIVITY_WRITE) {
    VerbSystem::setVerb(VerbSystem::Verb::Writing);
    return;
  }
  VerbSystem::setVerb(VerbSystem::Verb::Reading);
}

void onAgentEvent(const AgentEvents::Event& e) {
  if (strcmp(e.kind, "session.started") == 0) {
    VerbSystem::fireOverlay(VerbSystem::Verb::Waking, kOverlayMs, VerbSystem::Verb::None);
    EmotionSystem::setValence(+0.6f);
    EmotionSystem::setArousal(+0.6f);
    return;
  }
  if (strcmp(e.kind, "session.ended") == 0) {
    VerbSystem::setVerb(VerbSystem::Verb::Sleeping);
    return;
  }
  if (strcmp(e.kind, "turn.started") == 0) {
    if(VerbSystem::current() == VerbSystem::Verb::Sleeping) {
      VerbSystem::fireOverlay(VerbSystem::Verb::Waking, kOverlayMs, VerbSystem::Verb::Thinking);
    } else {
      VerbSystem::setVerb(VerbSystem::Verb::Thinking);
    }
    return;
  }
  if (strcmp(e.kind, "activity.started") == 0) {
    setVerbFromActivity(e);
    return;
  }
  if (strcmp(e.kind, "activity.finished") == 0 || strcmp(e.kind, "activity.failed") == 0) {
    VerbSystem::armLinger(kVerbLingerMs);
    return;
  }
  if (strcmp(e.kind, "turn.ended") == 0) {
    VerbSystem::clearVerb();
    EmotionSystem::impulse(+0.7f, +0.9f);
    return;
  }
  if (strcmp(e.kind, "notification") == 0) {
    const char* text = e.event["text"] | "";
    if (strncmp(text, "Claude needs", 12) == 0) {
      VerbSystem::fireOverlay(VerbSystem::Verb::AttractingAttention, kOverlayMs);
    }
    return;
  }
}

void onPaletteChange(Settings::NamedColor color, uint8_t r, uint8_t g, uint8_t b) {
  Settings::setColorRgb(color, r, g, b);
}

void onDisplayMode(BridgeControl::DisplayMode mode) {
  const bool face = (mode == BridgeControl::DisplayMode::Face);
  Settings::setFaceModeEnabled(face);
  switch (mode) {
    case BridgeControl::DisplayMode::Face:
      AgentEvents::setRenderMode(AgentEvents::RENDER_FACE);
      break;
    case BridgeControl::DisplayMode::Debug:
      AgentEvents::setRenderMode(AgentEvents::RENDER_DEBUG);
      break;
    case BridgeControl::DisplayMode::Text:
    default:
      AgentEvents::setRenderMode(AgentEvents::RENDER_TEXT);
      break;
  }
}

void onMotorsDisabled(bool motorsDisabled) {
  Settings::setMotorsDisabled(motorsDisabled);
  Motion::setEnabled(!motorsDisabled);
}

void onServoOverride(int8_t angle, uint32_t durationMs) { Motion::holdPosition(angle, durationMs); }

bool parseVerbFromVariant(JsonVariantConst v, VerbSystem::Verb* outVerb) {
  if (v.isNull()) return false;
  const char* text = v.as<const char*>();
  return VerbSystem::parseVerb(text, outVerb);
}

void dispatchRawCommand(JsonDocument& doc) {
  const char* action = getCommandAction(doc);
  JsonVariantConst params = getCommandParams(doc);
  if (!action || action[0] == '\0') return;

  if (strcmp(action, "startVerb") == 0) {
    VerbSystem::Verb verb;
    if (parseVerbFromVariant(params["verb"], &verb)) VerbSystem::setVerb(verb);
    return;
  }
  if (strcmp(action, "stopVerb") == 0 || strcmp(action, "clearVerb") == 0) {
    VerbSystem::clearVerb();
    return;
  }
  if (strcmp(action, "setOverlay") == 0) {
    VerbSystem::Verb verb;
    const uint32_t durationMs = (uint32_t)(params["duration_ms"] | kOverlayMs);
    if (!parseVerbFromVariant(params["verb"], &verb)) return;
    VerbSystem::Verb after;
    if (parseVerbFromVariant(params["after_verb"], &after) ||
        parseVerbFromVariant(params["post_verb"], &after)) {
      VerbSystem::fireOverlay(verb, durationMs, after);
    } else {
      VerbSystem::fireOverlay(verb, durationMs);
    }
    return;
  }
  if (strcmp(action, "modifyValence") == 0) {
    EmotionSystem::modifyValence((float)(params["delta_v"] | params["delta"] | 0.0f));
    return;
  }
  if (strcmp(action, "modifyArousal") == 0) {
    EmotionSystem::modifyArousal((float)(params["delta_a"] | params["delta"] | 0.0f));
    return;
  }
  if (strcmp(action, "setValence") == 0) {
    EmotionSystem::setValence((float)(params["v"] | params["value"] | 0.0f));
    return;
  }
  if (strcmp(action, "setArousal") == 0) {
    EmotionSystem::setArousal((float)(params["a"] | params["value"] | 0.0f));
    return;
  }
  if (strcmp(action, "setHeldValenceTarget") == 0) {
    const uint8_t driverId = (uint8_t)(params["driver_id"] | 0);
    const float targetV = (float)(params["target_v"] | 0.0f);
    EmotionSystem::setHeldTarget(driverId, targetV);
    return;
  }
  if (strcmp(action, "releaseHeldValenceTarget") == 0) {
    const uint8_t driverId = (uint8_t)(params["driver_id"] | 0);
    EmotionSystem::releaseHeldTarget(driverId);
    return;
  }
}

}  // namespace

void begin() {
  AgentEvents::begin();
  EmotionSystem::begin();
  VerbSystem::begin();
  AgentEvents::onEvent(&onAgentEvent);
  BridgeControl::onPaletteChange(&onPaletteChange);
  BridgeControl::onDisplayModeChange(&onDisplayMode);
  BridgeControl::onMotorsDisabledChange(&onMotorsDisabled);
  BridgeControl::onServoOverride(&onServoOverride);
  sPendingPermissionHeld = false;
  sStrainHeld = false;
  sLastSessionPollMs = 0;
  AgentEvents::setRenderMode(Settings::faceModeEnabled() ? AgentEvents::RENDER_FACE
                                                        : AgentEvents::RENDER_TEXT);
  Motion::setEnabled(!Settings::motorsDisabled());
}

void tick() {
  AgentEvents::tick();

  if (Bridge::isConnected() && AgentEvents::state().latched_session[0] == '\0') {
    const uint32_t now = millis();
    if (now - sLastSessionPollMs >= kSessionPollMs) {
      sLastSessionPollMs = now;
      Bridge::requestSessions();
    }
  }

  VerbSystem::tick();

  const bool pendingPermission = AgentEvents::state().pending_permission[0] != '\0';
  if (pendingPermission && !sPendingPermissionHeld) {
    EmotionSystem::setHeldTarget(EmotionSystem::Drivers::PendingPermission, kBlockedV);
    sPendingPermissionHeld = true;
  } else if (!pendingPermission && sPendingPermissionHeld) {
    EmotionSystem::releaseHeldTarget(EmotionSystem::Drivers::PendingPermission);
    sPendingPermissionHeld = false;
  }

  if (VerbSystem::current() == VerbSystem::Verb::Straining &&
      VerbSystem::timeInCurrentMs() >= kStrainStressDelayMs) {
    if (!sStrainHeld) {
      EmotionSystem::setHeldTarget(EmotionSystem::Drivers::Straining, kStrainV);
      sStrainHeld = true;
    }
  } else if (sStrainHeld) {
    EmotionSystem::releaseHeldTarget(EmotionSystem::Drivers::Straining);
    sStrainHeld = false;
  }

  EmotionSystem::tick();
}

void onBridgeMessage(JsonDocument& doc) {
  dispatchRawCommand(doc);
  AgentEvents::dispatch(doc);
  BridgeControl::dispatch(doc);
}

void onBridgeConnection(bool connected) { AgentEvents::notifyConnection(connected); }

}  // namespace EventRouter
