#include "AgentEvents.h"

#include <string.h>

#include "AsciiCopy.h"
#include "DebugLog.h"

namespace AgentEvents {

static AgentState g_state = {};

static PermissionRequestHandler   h_permReq = nullptr;
static PermissionResolvedHandler  h_permRes = nullptr;
static EventHandler               h_event   = nullptr;
static RawHandler                 h_raw     = nullptr;
static ConnectionHandler          h_conn    = nullptr;

const AgentState& state() { return g_state; }

void setWifiConnected(bool v) { g_state.wifi_connected = v; }
void setWsConnected(bool v)   { g_state.ws_connected   = v; }

void onPermissionRequest(PermissionRequestHandler h)  { h_permReq = h; }
void onPermissionResolved(PermissionResolvedHandler h){ h_permRes = h; }
void onEvent(EventHandler h)                          { h_event   = h; }
void onRaw(RawHandler h)                              { h_raw     = h; }
void onConnectionChange(ConnectionHandler h)          { h_conn    = h; }

void notifyConnection(bool connected) {
  g_state.ws_connected = connected;
  if (!connected) {
    g_state.working = false;
    g_state.current_tool[0] = '\0';
    g_state.tool_detail[0] = '\0';
    g_state.pending_permission[0] = '\0';
    g_state.pending_tool[0] = '\0';
    g_state.pending_detail[0] = '\0';
    g_state.latched_session[0] = '\0';
  }
  if (h_conn) h_conn(connected);
}

static bool isWriteActivity(const char* kind) {
  if (!kind || !*kind) return false;
  return !strcmp(kind, "file.write") ||
         !strcmp(kind, "file.delete") ||
         !strcmp(kind, "notebook.edit");
}

static bool latchFilter(const char* session_id, const char* event_kind) {
  if (!session_id || !*session_id) return true;

  if (g_state.latched_session[0] == '\0') {
    AsciiCopy::copy(g_state.latched_session, sizeof(g_state.latched_session), session_id);
    LOG_EVT("session latched via event: %s", session_id);
    return true;
  }

  if (strcmp(g_state.latched_session, session_id) != 0) {
    return false;
  }

  if (event_kind && !strcmp(event_kind, "session.ended")) {
    LOG_EVT("session unlatched (session.ended): %s", session_id);
    g_state.latched_session[0] = '\0';
  }
  return true;
}

static void handleAgentEvent(JsonDocument& doc) {
  JsonVariantConst evt = doc["event"].as<JsonVariantConst>();
  const char* kind = evt["kind"] | "";
  const char* session_id = doc["session_id"] | "";
  if (!latchFilter(session_id, kind)) return;

  if (session_id && *session_id) {
    AsciiCopy::copy(g_state.session_id, sizeof(g_state.session_id), session_id);
  }
  AsciiCopy::copy(g_state.agent, sizeof(g_state.agent), (const char*)(doc["agent"] | ""));
  AsciiCopy::copy(g_state.last_event_kind, sizeof(g_state.last_event_kind), kind);

  JsonVariantConst activity = evt["activity"].as<JsonVariantConst>();
  const char* activity_kind = activity["kind"] | "";
  const char* activity_tool = activity["tool"] | "";
  const char* activity_summary = activity["summary"] | "";
  if (activity_kind && *activity_kind) {
    AsciiCopy::copy(g_state.last_activity_kind, sizeof(g_state.last_activity_kind), activity_kind);
  }
  if (activity_tool && *activity_tool) {
    AsciiCopy::copy(g_state.last_tool, sizeof(g_state.last_tool), activity_tool);
  }

  if (!strcmp(kind, "turn.started")) {
    g_state.working = true;
    g_state.current_tool[0] = '\0';
    g_state.tool_detail[0] = '\0';
    g_state.current_tool_end_ms = 0;
    g_state.read_tools_this_turn = 0;
    g_state.write_tools_this_turn = 0;
    g_state.last_summary[0] = '\0';
  } else if (!strcmp(kind, "turn.ended") || !strcmp(kind, "session.ended")) {
    g_state.working = false;
    g_state.current_tool[0] = '\0';
    g_state.tool_detail[0] = '\0';
    g_state.current_tool_end_ms = 0;
  } else if (!strcmp(kind, "activity.started")) {
    g_state.working = true;
    AsciiCopy::copy(g_state.current_tool, sizeof(g_state.current_tool), activity_tool);
    AsciiCopy::copy(g_state.tool_detail, sizeof(g_state.tool_detail), activity_summary);
    g_state.current_tool_end_ms = 0;
    g_state.last_summary[0] = '\0';
  } else if (!strcmp(kind, "activity.finished") || !strcmp(kind, "activity.failed")) {
    g_state.current_tool_end_ms = millis();
    if (g_state.current_tool_end_ms == 0) g_state.current_tool_end_ms = 1;
    if (isWriteActivity(activity_kind)) {
      if (g_state.write_tools_this_turn < UINT16_MAX) g_state.write_tools_this_turn++;
    } else {
      if (g_state.read_tools_this_turn < UINT16_MAX) g_state.read_tools_this_turn++;
    }
  } else if (!strcmp(kind, "permission.requested")) {
    const char* request_id = evt["request_id"] | "";
    AsciiCopy::copy(g_state.pending_permission, sizeof(g_state.pending_permission), request_id);
    AsciiCopy::copy(g_state.pending_tool, sizeof(g_state.pending_tool), activity_tool);
    AsciiCopy::copy(g_state.pending_detail, sizeof(g_state.pending_detail), activity_summary);

    if (h_permReq) {
      PermissionRequestEvent e = { request_id, activity_tool, activity };
      h_permReq(e);
    }
  } else if (!strcmp(kind, "permission.resolved")) {
    const char* request_id = evt["request_id"] | "";
    const char* decision = evt["decision"] | "";
    if (!strcmp(request_id, g_state.pending_permission)) {
      g_state.pending_permission[0] = '\0';
      g_state.pending_tool[0] = '\0';
      g_state.pending_detail[0] = '\0';
    }
    if (h_permRes) {
      PermissionResolvedEvent e = { request_id, decision, true };
      h_permRes(e);
    }
  } else if (!strcmp(kind, "message.assistant")) {
    AsciiCopy::copy(g_state.last_summary, sizeof(g_state.last_summary), evt["text"] | "");
  } else if (!strcmp(kind, "notification")) {
    AsciiCopy::copy(g_state.last_summary, sizeof(g_state.last_summary), evt["text"] | "");
  } else if (!strcmp(kind, "thinking")) {
    AsciiCopy::copy(g_state.last_summary, sizeof(g_state.last_summary), evt["text"] | "");
  }

  if (h_event) {
    Event e = {
      kind,
      doc["agent"] | "",
      doc["session_id"] | "",
      doc["turn_id"] | "",
      activity_kind,
      activity_tool,
      activity_summary,
      evt,
      doc.as<JsonVariantConst>(),
    };
    h_event(e);
  }
}

static void onActiveSessionsFrame(JsonDocument& doc) {
  JsonVariantConst ids = doc["session_ids"];
  if (!ids.is<JsonArrayConst>()) return;

  if (g_state.latched_session[0] == '\0') {
    if (ids.size() > 0) {
      const char* first = ids[0] | "";
      if (*first) {
        AsciiCopy::copy(g_state.latched_session, sizeof(g_state.latched_session), first);
        LOG_EVT("session latched via active_sessions: %s", first);
      }
    }
    return;
  }

  bool found = false;
  for (size_t i = 0; i < ids.size(); ++i) {
    const char* s = ids[i] | "";
    if (*s && !strcmp(s, g_state.latched_session)) { found = true; break; }
  }
  if (!found) {
    LOG_EVT("session unlatched (not in active list): %s", g_state.latched_session);
    g_state.latched_session[0] = '\0';
  }
}

void dispatch(JsonDocument& doc) {
  g_state.last_event_ms = millis();
  const char* type = doc["type"] | "";
  if (!type || !*type) return;

  if (!strcmp(type, "agent_event")) {
    handleAgentEvent(doc);
  } else if (!strcmp(type, "active_sessions")) {
    onActiveSessionsFrame(doc);
  } else if (!strcmp(type, "pong")) {
    // keep alive
  } else if (!strcmp(type, "error")) {
    LOG_WARN("bridge error: %s", (const char*)(doc["message"] | ""));
  } else {
    LOG_WS("unknown type=%s", type);
  }

  if (h_raw) {
    RawEvent r = { type, doc.as<JsonVariantConst>() };
    h_raw(r);
  }
}

}  // namespace AgentEvents
