// Host implementation of bridge/BridgeClient.h. There is no socket here: the
// host app owns the WebSocket, injects received frames through
// sultana_core_inject(), reports connection changes through
// sultana_core_set_connected(), and receives outbound frames through the
// send callback.

#include "bridge/BridgeClient.h"

#include "host_internal.h"

namespace Bridge {

namespace {
bool gConnected = false;
MessageHandler gMessage = nullptr;
ConnectionHandler gConnection = nullptr;
}  // namespace

void begin(const char*, uint16_t, const char*) {}
void tick() {}
bool isConnected() { return gConnected; }
void onMessage(MessageHandler handler) { gMessage = handler; }
void onConnection(ConnectionHandler handler) { gConnection = handler; }

bool sendRaw(const char* json) {
  if (!gConnected || !json) return false;
  return SultanaHost::send(json);
}

bool requestSessions() { return sendRaw("{\"type\":\"request_sessions\"}"); }

}  // namespace Bridge

namespace SultanaHost {

void setConnected(bool connected) {
  if (Bridge::gConnected == connected) return;
  Bridge::gConnected = connected;
  if (Bridge::gConnection) Bridge::gConnection(connected);
}

void deliver(JsonDocument& doc) {
  if (Bridge::gMessage) Bridge::gMessage(doc);
}

}  // namespace SultanaHost
