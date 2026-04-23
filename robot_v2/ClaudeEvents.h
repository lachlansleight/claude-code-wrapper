#pragma once

// Event-like interface over the bridge's WebSocket stream.
//
// Two ways to consume bridge traffic:
//
//   1. Poll `ClaudeEvents::state()` — cheap, always up-to-date snapshot.
//      Good for "is Claude working?" / "what's the pending permission?" UI.
//
//   2. Register callbacks via `ClaudeEvents::on*()` — fired once per message.
//      Good for hook-driven side effects (tool-use reactions, etc.).
//
// BridgeClient is the only code that should call `dispatch()`. Everything
// else (including the .ino) talks to this module via getters and setters.

#include <Arduino.h>
#include <ArduinoJson.h>

namespace ClaudeEvents {

// ---- Event structs ---------------------------------------------------------

struct InboundMessageEvent {
  const char* content;
  const char* chat_id;
};

struct OutboundReplyEvent {
  const char* content;
  const char* chat_id;
};

struct PermissionRequestEvent {
  const char* request_id;   // 5-letter id [a-km-z], e.g. "abcde"
  const char* tool_name;
  JsonVariantConst input;   // tool input blob; may be null
};

struct PermissionResolvedEvent {
  const char* request_id;
  const char* behavior;     // "allow" | "deny"
  bool applied;             // false if the terminal user answered first
};

// Hook events cover PreToolUse, PostToolUse, UserPromptSubmit, Stop,
// SessionStart, SessionEnd, Notification, PreCompact, SubagentStop, etc.
// See https://docs.claude.com/en/docs/claude-code/hooks for the catalogue.
struct HookEvent {
  const char* hook_type;    // "PreToolUse", "Stop", ...
  const char* tool_name;    // payload.tool_name, or "" if absent
  const char* session_id;   // payload.session_id, or "" if absent
  JsonVariantConst payload; // full hook payload for advanced consumers
};

struct SessionEvent {
  const char* event;        // e.g. "started", "ended"
  const char* session_id;
};

struct RawEvent {
  const char* type;         // envelope "type" field
  JsonVariantConst doc;     // whole parsed message
};

// ---- Polled state ----------------------------------------------------------
//
// Updated by dispatch() before callbacks fire, so handlers see consistent
// state when they run.

struct ClaudeState {
  bool wifi_connected;
  bool ws_connected;
  bool working;                      // between UserPromptSubmit and Stop

  char session_id[40];               // last seen session id
  // Session this robot has latched onto. While set, hook events from other
  // sessions are ignored. Empty string means "not latched — latch onto the
  // next session we learn about."
  char latched_session[40];
  char last_hook[24];                // last hook_type
  char last_tool[32];                // last tool_name from hooks

  // Currently-running tool (set on PreToolUse). Stays populated after
  // PostToolUse so the display can linger on the last tool briefly; the
  // actual clear decision is made in Display based on `current_tool_end_ms`.
  char current_tool[24];
  char tool_detail[48];              // formatted per-tool metadata for display
  uint32_t current_tool_end_ms;      // millis() at PostToolUse, 0 if running

  // Most recent assistant text snippet (captured from hook transcripts).
  char last_summary[128];

  char pending_permission[8];        // request_id of unresolved permission, or ""
  char pending_tool[32];             // tool_name of unresolved permission
  char pending_detail[48];           // formatted per-tool metadata for permission

  uint32_t last_event_ms;            // millis() of most recent ws frame
};

const ClaudeState& state();
void setWifiConnected(bool v);
void setWsConnected(bool v);

// ---- Callbacks -------------------------------------------------------------
//
// One handler per event type. Passing nullptr clears the handler.

typedef void (*HelloHandler)(const char* client_id, const char* server_version);
typedef void (*InboundMessageHandler)(const InboundMessageEvent&);
typedef void (*OutboundReplyHandler)(const OutboundReplyEvent&);
typedef void (*PermissionRequestHandler)(const PermissionRequestEvent&);
typedef void (*PermissionResolvedHandler)(const PermissionResolvedEvent&);
typedef void (*HookHandler)(const HookEvent&);
typedef void (*SessionHandler)(const SessionEvent&);
typedef void (*RawHandler)(const RawEvent&);
typedef void (*ConnectionHandler)(bool connected);

void onHello(HelloHandler h);
void onInboundMessage(InboundMessageHandler h);
void onOutboundReply(OutboundReplyHandler h);
void onPermissionRequest(PermissionRequestHandler h);
void onPermissionResolved(PermissionResolvedHandler h);
void onHook(HookHandler h);
void onSession(SessionHandler h);
void onRaw(RawHandler h);
void onConnectionChange(ConnectionHandler h);

// ---- Dispatch --------------------------------------------------------------

// Called by BridgeClient with a parsed top-level message. Updates state,
// fires the type-specific callback (if any), and always fires onRaw.
void dispatch(JsonDocument& doc);

// Called by BridgeClient on WS connect/disconnect.
void notifyConnection(bool connected);

}  // namespace ClaudeEvents
