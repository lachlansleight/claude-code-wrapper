// SultanaCore implementation: wires the firmware modules together the way
// robot_v3.ino does, minus the hardware.

#include "sultana_core.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "agents/AgentEvents.h"
#include "app/EventRouter.h"
#include "app/SceneContextFill.h"
#include "behaviour/EmotionSystem.h"
#include "behaviour/VerbSystem.h"
#include "bridge/BridgeClient.h"
#include "face/FrameController.h"
#include "face/FrameEffective.h"
#include "face/SceneTypes.h"
#include "hal/Display.h"
#include "hal/Motion.h"
#include "hal/MotionBehaviors.h"
#include "hal/Settings.h"
#include "core/DebugLog.h"
#include "host_internal.h"

// ---- Clock ----------------------------------------------------------------

uint32_t g_sultana_millis = 1;

namespace {

constexpr uint32_t kClockOffsetMs = 10000;

void setClock(uint32_t nowMs) {
  const uint32_t t = nowMs + kClockOffsetMs;  // wraps like the device's millis()
  g_sultana_millis = (t == 0) ? 1 : t;
}

// ---- RNG ------------------------------------------------------------------

uint32_t gRngState = 1;

// ---- Callbacks ------------------------------------------------------------

sultana_send_fn gSend = nullptr;
void* gSendUser = nullptr;
sultana_log_fn gLog = nullptr;
void* gLogUser = nullptr;
std::string gLogLine;

// ---- Cached per-tick state --------------------------------------------------

Face::Expression gLastExpression = Face::Expression::Neutral;

void forceFaceMode() {
  if (AgentEvents::renderMode() != AgentEvents::RENDER_FACE) {
    AgentEvents::setRenderMode(AgentEvents::RENDER_FACE);
  }
}

void appendJsonString(std::string& out, const char* s) {
  out += '"';
  for (const char* p = s ? s : ""; *p; ++p) {
    const unsigned char c = (unsigned char)*p;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += (char)c;
    } else if (c < 0x20) {
      char esc[8];
      snprintf(esc, sizeof(esc), "\\u%04x", c);
      out += esc;
    } else {
      out += (char)c;
    }
  }
  out += '"';
}

}  // namespace

uint32_t sultana_random_u32() {
  // xorshift32
  uint32_t x = gRngState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  gRngState = x;
  return x;
}

void sultana_random_seed(uint32_t seed) { gRngState = seed ? seed : 1; }

void sultana_log_write(const char* text, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = text[i];
    if (c != '\n') {
      gLogLine += c;
      continue;
    }
    if (gLog) {
      gLog(gLogLine.c_str(), gLogUser);
    } else {
      fprintf(stderr, "%s\n", gLogLine.c_str());
    }
    gLogLine.clear();
  }
}

namespace SultanaHost {
bool send(const char* json) {
  if (!gSend) return false;
  gSend(json, gSendUser);
  return true;
}
}  // namespace SultanaHost

// ---- C API ------------------------------------------------------------------

extern "C" {

void sultana_core_init(uint32_t now_ms, uint32_t rng_seed) {
  setClock(now_ms);
  SultanaHost::setConnected(false);
  sultana_random_seed(rng_seed);

  // Same order as robot_v3.ino setup().
  Settings::begin();
  Display::begin();
  Motion::begin();
  MotionBehaviors::begin();
  Face::begin();

  EventRouter::begin();
  Bridge::onMessage(&EventRouter::onBridgeMessage);
  Bridge::onConnection(&EventRouter::onBridgeConnection);
  forceFaceMode();
  gLastExpression = Face::Expression::Neutral;
}

void sultana_core_set_send_callback(sultana_send_fn fn, void* user) {
  gSend = fn;
  gSendUser = user;
}

void sultana_core_set_log_callback(sultana_log_fn fn, void* user) {
  gLog = fn;
  gLogUser = user;
}

void sultana_core_set_connected(bool connected, uint32_t now_ms) {
  setClock(now_ms);
  SultanaHost::setConnected(connected);
}

bool sultana_core_inject(const char* json, size_t len, uint32_t now_ms) {
  if (!json) return false;
  setClock(now_ms);
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    LOG_WARN("json parse failed: %s", err.c_str());
    return false;
  }
  SultanaHost::deliver(doc);
  forceFaceMode();
  return true;
}

bool sultana_core_tick(uint32_t now_ms) {
  setClock(now_ms);
  const uint32_t before = SultanaHost::pushCount();

  // Same order as robot_v3.ino loop(), minus serial and WiFi.
  Bridge::tick();
  EventRouter::tick();
  Face::SceneContext ctx;
  SceneContextFill::fill(ctx);
  Face::tickEffectiveParams(ctx, millis());
  MotionBehaviors::tick(ctx);
  Motion::tick();
  Face::tick(ctx);

  gLastExpression = ctx.effective_expression;
  return SultanaHost::pushCount() != before;
}

const uint16_t* sultana_core_frame565(void) { return SultanaHost::pushedFrame(); }

uint32_t sultana_core_frame_count(void) { return SultanaHost::pushCount(); }

void sultana_core_frame_rgba(uint8_t* out) {
  if (!out) return;
  const uint16_t* src = SultanaHost::pushedFrame();
  for (int i = 0; i < SULTANA_SCREEN_WIDTH * SULTANA_SCREEN_HEIGHT; ++i) {
    const uint16_t p = src[i];
    const uint8_t r5 = (uint8_t)((p >> 11) & 0x1F);
    const uint8_t g6 = (uint8_t)((p >> 5) & 0x3F);
    const uint8_t b5 = (uint8_t)(p & 0x1F);
    out[i * 4 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
    out[i * 4 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
    out[i * 4 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    out[i * 4 + 3] = 255;
  }
}

int8_t sultana_core_arm_offset_deg(void) { return Motion::currentOffsetDeg(); }

size_t sultana_core_state_json(char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  const AgentEvents::AgentState& st = AgentEvents::state();
  const EmotionSystem::Emotion e = EmotionSystem::raw();
  char num[64];

  std::string s = "{\"verb\":";
  appendJsonString(s, VerbSystem::verbName(VerbSystem::current()));
  s += ",\"effective_verb\":";
  appendJsonString(s, VerbSystem::verbName(VerbSystem::effective()));
  s += ",\"expression\":";
  appendJsonString(s, Face::expressionName(gLastExpression));
  snprintf(num, sizeof(num), ",\"valence\":%.3f,\"activation\":%.3f", e.valence, e.activation);
  s += num;
  s += ",\"latched_session\":";
  appendJsonString(s, st.latched_session);
  s += ",\"pending_permission\":";
  appendJsonString(s, st.pending_permission);
  snprintf(num, sizeof(num), ",\"read_tools\":%u,\"write_tools\":%u", (unsigned)st.read_tools_this_turn,
           (unsigned)st.write_tools_this_turn);
  s += num;
  s += ",\"connected\":";
  s += Bridge::isConnected() ? "true" : "false";
  snprintf(num, sizeof(num), ",\"arm_deg\":%d,\"millis\":%u}", (int)Motion::currentOffsetDeg(),
           (unsigned)g_sultana_millis);
  s += num;

  const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
  memcpy(out, s.data(), n);
  out[n] = '\0';
  return n;
}

}  // extern "C"
