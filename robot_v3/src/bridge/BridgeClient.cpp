#include "BridgeClient.h"

#include <WebSocketsClient.h>

#include "../core/DebugLog.h"

namespace Bridge {

namespace {

static constexpr uint32_t kHeartbeatMs = 15000;
static constexpr uint32_t kHeartbeatTimeoutMs = 3000;
static constexpr uint8_t kHeartbeatFails = 2;
static constexpr uint32_t kReconnectMs = 2000;

WebSocketsClient ws;
bool connected = false;
MessageHandler sMessageHandler = nullptr;
ConnectionHandler sConnectionHandler = nullptr;

void handleText(uint8_t* payload, size_t length) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    LOG_WARN("json parse failed: %s", err.c_str());
    return;
  }
  if (sMessageHandler) sMessageHandler(doc);
}

void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connected = true;
      LOG_WS("connected url=%s", (const char*)payload);
      if (sConnectionHandler) sConnectionHandler(true);
      break;
    case WStype_DISCONNECTED:
      connected = false;
      LOG_WS("disconnected");
      if (sConnectionHandler) sConnectionHandler(false);
      break;
    case WStype_TEXT:
      handleText(payload, length);
      break;
    case WStype_ERROR:
      LOG_WARN("ws error: %.*s", (int)length, payload);
      break;
    default:
      break;
  }
}

}  // namespace

void begin(const char* host, uint16_t port, const char* token) {
  String path = String("/ws?token=") + token;
  ws.begin(host, port, path.c_str());
  ws.onEvent(onWsEvent);
  ws.setReconnectInterval(kReconnectMs);
  ws.enableHeartbeat(kHeartbeatMs, kHeartbeatTimeoutMs, kHeartbeatFails);
  LOG_INFO("bridge client -> %s:%u", host, (unsigned)port);
}

void tick() { ws.loop(); }

bool isConnected() { return connected; }

void onMessage(MessageHandler handler) { sMessageHandler = handler; }
void onConnection(ConnectionHandler handler) { sConnectionHandler = handler; }

bool sendRaw(const char* json) {
  if (!connected) return false;
  return ws.sendTXT(json);
}

bool requestSessions() { return sendRaw("{\"type\":\"request_sessions\"}"); }

}  // namespace Bridge
