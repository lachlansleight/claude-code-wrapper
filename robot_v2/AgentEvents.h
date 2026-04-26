#pragma once

// General-purpose event interface over the bridge WebSocket stream.
//
// Two ways to consume bridge traffic:
//   1. Poll `AgentEvents::state()` for an always-updated snapshot.
//   2. Register callbacks via `AgentEvents::on*()` for edge-driven logic.

#include <Arduino.h>
#include <ArduinoJson.h>

namespace AgentEvents {

enum ActivityAccess : uint8_t {
  ACTIVITY_READ = 0,
  ACTIVITY_WRITE,
};

struct PermissionRequestEvent {
  const char* request_id;
  const char* tool_name;
  JsonVariantConst input;
};

struct PermissionResolvedEvent {
  const char* request_id;
  const char* behavior;  // "allow" | "deny"
  bool applied;
};

// Unified event payload from bridge `type:"agent_event"` envelopes.
struct Event {
  const char* kind;             // event.kind
  const char* agent;            // envelope.agent
  const char* session_id;       // envelope.session_id
  const char* turn_id;          // envelope.turn_id
  const char* activity_kind;    // event.activity.kind (when present)
  const char* activity_tool;    // event.activity.tool (when present)
  const char* activity_summary; // event.activity.summary (when present)
  JsonVariantConst event;       // envelope.event object
  JsonVariantConst envelope;    // full envelope
};

struct RawEvent {
  const char* type;         // envelope "type" field
  JsonVariantConst doc;     // whole parsed message
};

struct AgentState {
  bool wifi_connected;
  bool ws_connected;
  bool working;

  char agent[16];                   // last seen envelope.agent
  char session_id[40];              // last seen session id
  char latched_session[40];         // active session this robot follows
  char last_event_kind[40];         // last event.kind
  char last_activity_kind[24];      // last activity.kind
  char last_tool[32];               // last activity.tool

  // Current activity and optional summary text. Field names intentionally
  // preserve old consumers that still refer to "tool".
  char current_tool[32];
  char tool_detail[80];
  uint32_t current_tool_end_ms;     // millis() at activity.finished/failed

  // Most recent assistant-facing text snippet.
  char last_summary[128];

  // Activity counters since the last turn.started, split by read/write.
  uint16_t read_tools_this_turn;
  uint16_t write_tools_this_turn;

  char pending_permission[48];
  char pending_tool[32];
  char pending_detail[80];

  uint32_t last_event_ms;
};

const AgentState& state();
void setWifiConnected(bool v);
void setWsConnected(bool v);

typedef void (*PermissionRequestHandler)(const PermissionRequestEvent&);
typedef void (*PermissionResolvedHandler)(const PermissionResolvedEvent&);
typedef void (*EventHandler)(const Event&);
typedef void (*RawHandler)(const RawEvent&);
typedef void (*ConnectionHandler)(bool connected);

void onPermissionRequest(PermissionRequestHandler h);
void onPermissionResolved(PermissionResolvedHandler h);
void onEvent(EventHandler h);
void onRaw(RawHandler h);
void onConnectionChange(ConnectionHandler h);

// Unified read/write classification used for both counters and behavior
// routing. "Write" means likely to modify on-disk data.
ActivityAccess classifyActivity(const char* activity_kind,
                                const char* activity_tool,
                                const char* activity_summary);

void dispatch(JsonDocument& doc);
void notifyConnection(bool connected);

}  // namespace AgentEvents
