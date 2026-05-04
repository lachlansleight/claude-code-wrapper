#include "AgentEvents.h"

#include <string.h>

#include "../core/AsciiCopy.h"

namespace AgentEvents {

namespace {

AgentState sState = {};
EventHandler sEventHandler = nullptr;
ConnectionHandler sConnectionHandler = nullptr;

void copyField(char* dst, size_t cap, const char* src) {
  AsciiCopy::copy(dst, cap, src ? src : "");
}

}  // namespace

void begin() { memset(&sState, 0, sizeof(sState)); }

const AgentState& state() { return sState; }

void onEvent(EventHandler handler) { sEventHandler = handler; }
void onConnectionChange(ConnectionHandler handler) { sConnectionHandler = handler; }

void notifyConnection(bool connected) {
  sState.wsConnected = connected;
  if (sConnectionHandler) sConnectionHandler(connected);
}

void dispatch(JsonDocument& doc) {
  const char* type = doc["type"] | "";
  if (strcmp(type, "active_sessions") == 0) {
    JsonArrayConst sessions = doc["sessions"].as<JsonArrayConst>();
    if (!sessions.isNull() && sessions.size() > 0) {
      const char* id = sessions[0]["id"] | "";
      copyField(sState.latchedSession, sizeof(sState.latchedSession), id);
    }
    return;
  }

  if (strcmp(type, "agent_event") != 0) return;

  JsonObjectConst envelope = doc.as<JsonObjectConst>();
  JsonObjectConst eventObj = envelope["event"].as<JsonObjectConst>();
  const char* kind = eventObj["kind"] | "";
  copyField(sState.lastEventKind, sizeof(sState.lastEventKind), kind);

  if (strcmp(kind, "permission.requested") == 0) {
    copyField(sState.pendingPermission, sizeof(sState.pendingPermission),
              eventObj["request_id"] | "");
  } else if (strcmp(kind, "permission.resolved") == 0 || strcmp(kind, "turn.started") == 0) {
    sState.pendingPermission[0] = '\0';
  }

  if (!sEventHandler) return;
  Event evt = {
      kind,
      envelope["agent"] | "",
      envelope["session_id"] | "",
      envelope["turn_id"] | "",
      envelope["event"],
      envelope,
  };
  sEventHandler(evt);
}

}  // namespace AgentEvents
