#include "ClaudeEvents.h"

#include <string.h>

#include "DebugLog.h"

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

static void copyStr(char* dst, size_t cap, const char* src) {
  if (!dst || cap == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
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
    g_state.pending_permission[0] = '\0';
    g_state.pending_tool[0] = '\0';
  }
  if (h_conn) h_conn(connected);
}

static void handleHookUpdate(const HookEvent& evt) {
  copyStr(g_state.last_hook, sizeof(g_state.last_hook), evt.hook_type);
  if (evt.tool_name && *evt.tool_name) {
    copyStr(g_state.last_tool, sizeof(g_state.last_tool), evt.tool_name);
  }
  if (evt.session_id && *evt.session_id) {
    copyStr(g_state.session_id, sizeof(g_state.session_id), evt.session_id);
  }

  // Track the "working" flag from the hook lifecycle. UserPromptSubmit opens
  // a turn; Stop / SessionEnd close it. Best-effort — hooks may be dropped.
  if (!strcmp(evt.hook_type, "UserPromptSubmit")) {
    g_state.working = true;
  } else if (!strcmp(evt.hook_type, "Stop") ||
             !strcmp(evt.hook_type, "SessionEnd")) {
    g_state.working = false;
  }
}

void dispatch(JsonDocument& doc) {
  g_state.last_event_ms = millis();

  const char* type = doc["type"] | "";
  if (!type || !*type) return;

  if (!strcmp(type, "hello")) {
    const char* cid = doc["client_id"]     | "";
    const char* ver = doc["server_version"]| "";
    LOG_EVT("hello client_id=%s server=%s", cid, ver);
    if (h_hello) h_hello(cid, ver);
  }
  else if (!strcmp(type, "inbound_message")) {
    InboundMessageEvent e = {
      doc["content"] | "",
      doc["chat_id"] | "",
    };
    copyStr(g_state.last_msg, sizeof(g_state.last_msg), e.content);
    LOG_EVT("inbound chat=%s \"%.40s\"", e.chat_id, e.content);
    if (h_inbound) h_inbound(e);
  }
  else if (!strcmp(type, "outbound_reply")) {
    OutboundReplyEvent e = {
      doc["content"] | "",
      doc["chat_id"] | "",
    };
    copyStr(g_state.last_msg, sizeof(g_state.last_msg), e.content);
    LOG_EVT("outbound chat=%s \"%.40s\"", e.chat_id, e.content);
    if (h_outbound) h_outbound(e);
  }
  else if (!strcmp(type, "permission_request")) {
    PermissionRequestEvent e = {
      doc["request_id"] | "",
      doc["tool_name"]  | "",
      doc["input"].as<JsonVariantConst>(),
    };
    copyStr(g_state.pending_permission, sizeof(g_state.pending_permission), e.request_id);
    copyStr(g_state.pending_tool, sizeof(g_state.pending_tool), e.tool_name);
    LOG_EVT("perm-req id=%s tool=%s", e.request_id, e.tool_name);
    if (h_permReq) h_permReq(e);
  }
  else if (!strcmp(type, "permission_resolved")) {
    PermissionResolvedEvent e = {
      doc["request_id"] | "",
      doc["behavior"]   | "",
      (bool)(doc["applied"] | false),
    };
    if (!strcmp(e.request_id, g_state.pending_permission)) {
      g_state.pending_permission[0] = '\0';
      g_state.pending_tool[0] = '\0';
    }
    LOG_EVT("perm-res id=%s %s applied=%d", e.request_id, e.behavior, e.applied);
    if (h_permRes) h_permRes(e);
  }
  else if (!strcmp(type, "hook_event")) {
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
  else if (!strcmp(type, "session_event")) {
    SessionEvent e = {
      doc["event"]      | "",
      doc["session_id"] | "",
    };
    if (e.session_id && *e.session_id) {
      copyStr(g_state.session_id, sizeof(g_state.session_id), e.session_id);
    }
    LOG_EVT("session %s id=%s", e.event, e.session_id);
    if (h_session) h_session(e);
  }
  else if (!strcmp(type, "pong")) {
    // keep-alive, no-op
  }
  else if (!strcmp(type, "error")) {
    LOG_WARN("bridge error: %s", (const char*)(doc["message"] | ""));
  }
  else {
    LOG_WS("unknown type=%s", type);
  }

  if (h_raw) {
    RawEvent r = { type, doc.as<JsonVariantConst>() };
    h_raw(r);
  }
}

}  // namespace ClaudeEvents
