#include "BridgeClient.h"

#include <ArduinoJson.h>
#include <WebSocketsClient.h>

#include "AgentEvents.h"
#include "DebugLog.h"
#include "config.h"

namespace Bridge {

// WebSocket ping cadence. The bridge doesn't require it, but it lets us
// notice broken connections within a few seconds instead of hanging.
static constexpr uint32_t kHeartbeatMs     = 15000;
static constexpr uint32_t kHeartbeatToutMs =  3000;
static constexpr uint8_t  kHeartbeatFails  =     2;
static constexpr uint32_t kReconnectMs     =  2000;

// How often we poll the bridge for an active session to latch onto, while
// no session is latched. Sent as `{"type":"request_sessions"}`; the bridge
// replies with an `active_sessions` frame.
static constexpr uint32_t kSessionPollMs   =  5000;

// Decoded messages can get chunky (hook payloads carry transcript data).
// 8 KiB is a comfortable ceiling; bump if ArduinoJson reports NoMemory.
static constexpr size_t kDocCapacity = 16384;

static WebSocketsClient ws;
static bool connected = false;

static void handleText(uint8_t* payload, size_t length) {
#if DEBUG_WS_VERBOSE
  // Dump the raw frame before parsing so you can inspect traffic even if
  // the JSON parser chokes.
  Serial.printf("[ws   ] << %.*s\n", (int)length, payload);
#endif

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    LOG_WARN("json parse failed: %s", err.c_str());
    return;
  }
  AgentEvents::dispatch(doc);
}

static void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connected = true;
      LOG_WS("connected url=%s", (const char*)payload);
      AgentEvents::notifyConnection(true);
      break;
    case WStype_DISCONNECTED:
      connected = false;
      LOG_WS("disconnected");
      AgentEvents::notifyConnection(false);
      break;
    case WStype_TEXT:
      handleText(payload, length);
      break;
    case WStype_BIN:
      LOG_WS("binary frame len=%u (ignored)", (unsigned)length);
      break;
    case WStype_ERROR:
      LOG_WARN("ws error: %.*s", (int)length, payload);
      break;
    case WStype_PING:
    case WStype_PONG:
    default:
      break;
  }
}

void begin(const char* host, uint16_t port, const char* token) {
  String path = String("/ws?token=") + token;
  ws.begin(host, port, path.c_str());
  ws.onEvent(onWsEvent);
  ws.setReconnectInterval(kReconnectMs);
  ws.enableHeartbeat(kHeartbeatMs, kHeartbeatToutMs, kHeartbeatFails);
  LOG_INFO("bridge client → %s:%u", host, (unsigned)port);
}

void tick() {
  ws.loop();

  // While unlatched, nudge the bridge every 5s for the current session list.
  // Bridge also pushes on change, but polling catches the case where the
  // only session existed before we connected and no further changes fire.
  static uint32_t lastPoll = 0;
  if (connected && AgentEvents::state().latched_session[0] == '\0') {
    const uint32_t now = millis();
    if (now - lastPoll >= kSessionPollMs) {
      lastPoll = now;
      sendRaw("{\"type\":\"request_sessions\"}");
    }
  }
}

bool isConnected() { return connected; }

bool sendRaw(const char* json) {
  if (!connected) return false;
  return ws.sendTXT(json);
}

bool sendPermissionVerdict(const char* request_id, const char* behavior) {
  JsonDocument doc;
  doc["type"] = "permission_verdict";
  doc["request_id"] = request_id;
  doc["behavior"] = behavior;
  char buf[160];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0) return false;
  LOG_EVT(">> verdict id=%s %s", request_id, behavior);
  return sendRaw(buf);
}

bool sendChatMessage(const char* content, const char* chat_id) {
  JsonDocument doc;
  doc["type"] = "send_message";
  doc["content"] = content;
  if (chat_id && *chat_id) doc["chat_id"] = chat_id;
  char buf[512];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0) return false;
  return sendRaw(buf);
}

}  // namespace Bridge
