#pragma once

// Parses bridge JSON into AgentState + typed callbacks. No display, motor,
// or NVS side effects — composition root wires those from control frames.

#include <Arduino.h>
#include <ArduinoJson.h>

namespace AgentEvents {

enum ActivityAccess : uint8_t {
  ACTIVITY_READ = 0,
  ACTIVITY_WRITE,
};

enum RenderMode : uint8_t {
  RENDER_FACE = 0,
  RENDER_TEXT,
};

struct PermissionRequestEvent {
  const char* request_id;
  const char* tool_name;
  JsonVariantConst input;
};

struct PermissionResolvedEvent {
  const char* request_id;
  const char* behavior;
  bool applied;
};

struct Event {
  const char* kind;
  const char* agent;
  const char* session_id;
  const char* turn_id;
  const char* activity_kind;
  const char* activity_tool;
  const char* activity_summary;
  JsonVariantConst event;
  JsonVariantConst envelope;
};

struct RawEvent {
  const char* type;
  JsonVariantConst doc;
};

struct AgentState {
  bool wifi_connected;
  bool ws_connected;
  bool working;

  char agent[16];
  char session_id[40];
  char latched_session[40];
  char last_event_kind[40];
  char last_activity_kind[24];
  char last_tool[32];

  char current_tool[32];
  char tool_detail[80];
  uint32_t current_tool_end_ms;

  char last_summary[128];
  char status_line[80];
  char subtitle_tool[320];
  char body_text[512];
  uint32_t thinking_title_since_ms;
  uint32_t turn_started_wall_ms;
  uint32_t done_turn_elapsed_ms;
  char latest_shell_command[160];
  char latest_read_target[160];
  char latest_write_target[160];
  char thought_lines[4][96];
  uint8_t thought_count;
  uint32_t body_updated_ms;
  uint32_t thought_updated_ms;
  uint32_t text_tool_linger_until_ms;
  RenderMode render_mode;

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

using PermissionRequestHandler = void (*)(const PermissionRequestEvent&);
using PermissionResolvedHandler = void (*)(const PermissionResolvedEvent&);
using EventHandler = void (*)(const Event&);
using RawHandler = void (*)(const RawEvent&);
using ConnectionHandler = void (*)(bool connected);

void onPermissionRequest(PermissionRequestHandler h);
void onPermissionResolved(PermissionResolvedHandler h);
void onEvent(EventHandler h);
void onRaw(RawHandler h);
void onConnectionChange(ConnectionHandler h);

ActivityAccess classifyActivity(const char* activity_kind,
                                const char* activity_tool,
                                const char* activity_summary);
RenderMode renderMode();
void setRenderMode(RenderMode mode);

void begin();
void dispatch(JsonDocument& doc);
void tick();
void notifyConnection(bool connected);
void clearTextDisplayForSleep();

}  // namespace AgentEvents
