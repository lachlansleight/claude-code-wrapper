#include "ClaudeEvents.h"

#include <string.h>

#include "AsciiCopy.h"
#include "DebugLog.h"
#include "ToolFormat.h"

namespace ClaudeEvents {

static ClaudeState g_state = {};

static HelloHandler               h_hello       = nullptr;
static InboundMessageHandler      h_inbound     = nullptr;
static OutboundReplyHandler       h_outbound    = nullptr;
static PermissionRequestHandler   h_permReq     = nullptr;
static PermissionResolvedHandler  h_permRes     = nullptr;
static HookHandler                h_hook        = nullptr;
static SessionHandler             h_session     = nullptr;
static RawHandler                 h_raw         = nullptr;
static ConnectionHandler          h_conn        = nullptr;

// Grab the last assistant text block from a hook payload (hook-forward.ts
// attaches `assistant_text` as a string array on PostToolUse / Stop).
static void captureAssistantSummary(JsonVariantConst payload,
                                    char* out, size_t cap) {
  JsonVariantConst arr = payload["assistant_text"];
  if (!arr.is<JsonArrayConst>()) return;
  size_t n = arr.size();
  if (n == 0) return;
  const char* last = arr[n - 1] | "";
  if (*last) AsciiCopy::copy(out, cap, last);
}

const ClaudeState& state() { return g_state; }

void setWifiConnected(bool v) { g_state.wifi_connected = v; }
void setWsConnected(bool v)   { g_state.ws_connected   = v; }

void onHello(HelloHandler h)                         { h_hello   = h; }
void onInboundMessage(InboundMessageHandler h)       { h_inbound = h; }
void onOutboundReply(OutboundReplyHandler h)         { h_outbound= h; }
void onPermissionRequest(PermissionRequestHandler h) { h_permReq = h; }
void onPermissionResolved(PermissionResolvedHandler h){h_permRes = h; }
void onHook(HookHandler h)                           { h_hook    = h; }
void onSession(SessionHandler h)                     { h_session = h; }
void onRaw(RawHandler h)                             { h_raw     = h; }
void onConnectionChange(ConnectionHandler h)         { h_conn    = h; }

void notifyConnection(bool connected) {
  g_state.ws_connected = connected;
  if (!connected) {
    g_state.working = false;
    g_state.current_tool[0] = '\0';
    g_state.tool_detail[0] = '\0';
    g_state.pending_permission[0] = '\0';
    g_state.pending_tool[0] = '\0';
    g_state.pending_detail[0] = '\0';
    // Drop the latch on disconnect — on reconnect we'll re-poll for whatever
    // sessions are currently active and latch onto the first one.
    g_state.latched_session[0] = '\0';
  }
  if (h_conn) h_conn(connected);
}

// Returns true if this event's session matches whatever we've latched onto
// (or if we just latched onto it). False means the event is from some other
// session and should be ignored.
static bool latchFilter(const char* session_id, const char* hook_type) {
  if (!session_id || !*session_id) {
    // No session info — pass through (can't filter what we can't identify).
    return true;
  }
  if (g_state.latched_session[0] == '\0') {
    AsciiCopy::copy(g_state.latched_session, sizeof(g_state.latched_session), session_id);
    LOG_EVT("session latched via hook: %s", session_id);
    return true;
  }
  if (strcmp(g_state.latched_session, session_id) != 0) {
    return false;  // different session, drop
  }
  // Matched. If this is the latched session ending, release the latch so
  // we fall back to polling for another.
  if (hook_type && !strcmp(hook_type, "SessionEnd")) {
    LOG_EVT("session unlatched (SessionEnd): %s", session_id);
    g_state.latched_session[0] = '\0';
  }
  return true;
}

static void handleHookUpdate(const HookEvent& evt) {
  if (!latchFilter(evt.session_id, evt.hook_type)) return;

  AsciiCopy::copy(g_state.last_hook, sizeof(g_state.last_hook), evt.hook_type);
  if (evt.tool_name && *evt.tool_name) {
    AsciiCopy::copy(g_state.last_tool, sizeof(g_state.last_tool), evt.tool_name);
  }
  if (evt.session_id && *evt.session_id) {
    AsciiCopy::copy(g_state.session_id, sizeof(g_state.session_id), evt.session_id);
  }

  const char* h = evt.hook_type;

  // Turn lifecycle: UserPromptSubmit opens a turn; Stop / SessionEnd close
  // it. PreToolUse also asserts `working=true` defensively — if Stop flaps
  // mid-response, a subsequent tool call drags us back out of idle so the
  // attract scheduler can't latch onto a phantom idle window.
  if (!strcmp(h, "UserPromptSubmit")) {
    g_state.working = true;
    g_state.current_tool[0] = '\0';
    g_state.tool_detail[0] = '\0';
    g_state.current_tool_end_ms = 0;
    // Wipe the previous turn's summary so the body doesn't display stale
    // text while Claude is thinking about the new prompt.
    g_state.last_summary[0] = '\0';
  } else if (!strcmp(h, "Stop") || !strcmp(h, "SessionEnd")) {
    g_state.working = false;
    g_state.current_tool[0] = '\0';
    g_state.tool_detail[0] = '\0';
    g_state.current_tool_end_ms = 0;
  }

  // Tool lifecycle: PreToolUse(X) → current=X running; PostToolUse(X) →
  // stamp end time but leave label so the display can linger.
  JsonVariantConst input = evt.payload["tool_input"];
  if (!strcmp(h, "PreToolUse")) {
    g_state.working = true;
    AsciiCopy::copy(g_state.current_tool, sizeof(g_state.current_tool), evt.tool_name);
    ToolFormat::detail(evt.tool_name, input,
                       g_state.tool_detail, sizeof(g_state.tool_detail));
    g_state.current_tool_end_ms = 0;
    // A new tool displaces any prior notification/summary — otherwise it'd
    // resurface on screen once the tool slot lingers out.
    g_state.last_summary[0] = '\0';
  } else if (!strcmp(h, "PostToolUse")) {
    g_state.current_tool_end_ms = millis();
    if (g_state.current_tool_end_ms == 0) g_state.current_tool_end_ms = 1;
  }

  captureAssistantSummary(evt.payload,
                          g_state.last_summary, sizeof(g_state.last_summary));
}

// ---- Envelope handlers ----------------------------------------------------
//
// One per top-level `type` so dispatch() reads as a flat table.

static void onHelloFrame(JsonDocument& doc) {
  const char* cid = doc["client_id"]     | "";
  const char* ver = doc["server_version"]| "";
  LOG_EVT("hello client_id=%s server=%s", cid, ver);
  if (h_hello) h_hello(cid, ver);
}

static void onInboundFrame(JsonDocument& doc) {
  InboundMessageEvent e = {
    doc["content"] | "",
    doc["chat_id"] | "",
  };
  AsciiCopy::copy(g_state.last_summary, sizeof(g_state.last_summary), e.content);
  g_state.current_tool[0] = '\0';
  g_state.tool_detail[0] = '\0';
  g_state.current_tool_end_ms = 0;
  LOG_EVT("inbound chat=%s \"%.40s\"", e.chat_id, e.content);
  if (h_inbound) h_inbound(e);
}

static void onOutboundFrame(JsonDocument& doc) {
  OutboundReplyEvent e = {
    doc["content"] | "",
    doc["chat_id"] | "",
  };
  AsciiCopy::copy(g_state.last_summary, sizeof(g_state.last_summary), e.content);
  g_state.current_tool[0] = '\0';
  g_state.tool_detail[0] = '\0';
  g_state.current_tool_end_ms = 0;
  LOG_EVT("outbound chat=%s \"%.40s\"", e.chat_id, e.content);
  if (h_outbound) h_outbound(e);
}

static void onPermissionRequestFrame(JsonDocument& doc) {
  PermissionRequestEvent e = {
    doc["request_id"] | "",
    doc["tool_name"]  | "",
    doc["input"].as<JsonVariantConst>(),
  };
  AsciiCopy::copy(g_state.pending_permission, sizeof(g_state.pending_permission), e.request_id);
  AsciiCopy::copy(g_state.pending_tool, sizeof(g_state.pending_tool), e.tool_name);
  ToolFormat::detail(e.tool_name, e.input,
                     g_state.pending_detail, sizeof(g_state.pending_detail));
  LOG_EVT("perm-req id=%s tool=%s", e.request_id, e.tool_name);
  if (h_permReq) h_permReq(e);
}

static void onPermissionResolvedFrame(JsonDocument& doc) {
  PermissionResolvedEvent e = {
    doc["request_id"] | "",
    doc["behavior"]   | "",
    (bool)(doc["applied"] | false),
  };
  if (!strcmp(e.request_id, g_state.pending_permission)) {
    g_state.pending_permission[0] = '\0';
    g_state.pending_tool[0] = '\0';
    g_state.pending_detail[0] = '\0';
  }
  LOG_EVT("perm-res id=%s %s applied=%d", e.request_id, e.behavior, e.applied);
  if (h_permRes) h_permRes(e);
}

static void onHookFrame(JsonDocument& doc) {
  JsonVariantConst payload = doc["payload"].as<JsonVariantConst>();
  HookEvent e = {
    doc["hook_type"] | "",
    payload["tool_name"]  | "",
    payload["session_id"] | "",
    payload,
  };
  handleHookUpdate(e);
  LOG_EVT("hook %s tool=%s", e.hook_type, e.tool_name);
  if (h_hook) h_hook(e);
}

static void onSessionEventFrame(JsonDocument& doc) {
  SessionEvent e = {
    doc["event"]      | "",
    doc["session_id"] | "",
  };
  if (e.session_id && *e.session_id) {
    AsciiCopy::copy(g_state.session_id, sizeof(g_state.session_id), e.session_id);
  }
  LOG_EVT("session %s id=%s", e.event, e.session_id);
  if (h_session) h_session(e);
}

// Bridge-pushed list of currently-active sessions. If we're unlatched, grab
// the first id. If we're already latched but our id isn't on the list any
// more (e.g. SessionEnd hook was lost), drop the latch so polling picks up
// a new one.
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

  if      (!strcmp(type, "hello"))               onHelloFrame(doc);
  else if (!strcmp(type, "inbound_message"))     onInboundFrame(doc);
  else if (!strcmp(type, "outbound_reply"))      onOutboundFrame(doc);
  else if (!strcmp(type, "permission_request"))  onPermissionRequestFrame(doc);
  else if (!strcmp(type, "permission_resolved")) onPermissionResolvedFrame(doc);
  else if (!strcmp(type, "hook_event"))          onHookFrame(doc);
  else if (!strcmp(type, "session_event"))       onSessionEventFrame(doc);
  else if (!strcmp(type, "active_sessions"))     onActiveSessionsFrame(doc);
  else if (!strcmp(type, "pong"))                { /* keep-alive */ }
  else if (!strcmp(type, "error"))               LOG_WARN("bridge error: %s", (const char*)(doc["message"] | ""));
  else                                           LOG_WS("unknown type=%s", type);

  if (h_raw) {
    RawEvent r = { type, doc.as<JsonVariantConst>() };
    h_raw(r);
  }
}

}  // namespace ClaudeEvents
