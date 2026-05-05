#pragma once

// Parses bridge JSON into AgentState + typed callbacks. No display, motor,
// or NVS side effects — composition root wires those from control frames.

#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * @file AgentEvents.h
 * @brief Semantic parser for bridge `agent_event` envelopes.
 *
 * The bridge speaks a small set of frame types over WebSocket. The
 * `agent_event` type carries the canonical lifecycle vocabulary —
 * `turn.started`, `activity.started/finished/failed`,
 * `permission.requested/resolved`, `message.assistant`, `thinking`,
 * `notification`, `session.started/ended`. AgentEvents is responsible
 * for turning each into:
 *
 *  1. **Updates to the singleton AgentState** — a flat, polled snapshot
 *     of "what is the agent doing right now" used by every renderer.
 *     Strings are sanitized through AsciiCopy so they are display-safe.
 *  2. **Side-effect-free callbacks** for downstream systems that want
 *     edge events (permission, raw event, connection change). The
 *     composition root (`EventRouter`) wires these into VerbSystem,
 *     EmotionSystem, etc.
 *
 * AgentEvents is intentionally **pure** — no Display/Motion/NVS calls.
 * It also handles a small piece of session-management logic: "session
 * latching", which keeps the firmware focused on a single session at a
 * time even when the bridge multiplexes many. The first session seen
 * is latched; further sessions are filtered out unless the active
 * session list says the latched one is gone.
 *
 * `tick()` drives the **text-tool linger** — when an activity finishes
 * we hold the tool subtitle on screen for ~1 s before falling back to
 * "Thinking", so bursts of fast tools don't strobe the UI.
 */
namespace AgentEvents {

/// Coarse read-vs-write classification for an agent activity.
enum ActivityAccess : uint8_t {
  ACTIVITY_READ = 0,   ///< Pure read (file.read, most shell, default).
  ACTIVITY_WRITE,      ///< Mutates state (file.write/delete, notebook.edit, write-y shell).
};

/// Top-level rendering style. Persisted in Settings as the face/text mode.
enum RenderMode : uint8_t {
  RENDER_FACE = 0,  ///< Procedural face + arm motion.
  RENDER_TEXT,      ///< Minimal text-only status display.
  RENDER_DEBUG,     ///< Verbose debug overlay.
};

/// Edge event: a permission request just arrived from the bridge.
struct PermissionRequestEvent {
  const char* request_id;     ///< Stable id used to match a later resolve.
  const char* tool_name;      ///< Tool that triggered the prompt (e.g. "Bash").
  JsonVariantConst input;     ///< Full activity object (kind, summary, ...).
};

/// Edge event: the bridge resolved a previously-pending permission.
struct PermissionResolvedEvent {
  const char* request_id;     ///< Matches a prior PermissionRequestEvent.
  const char* behavior;       ///< Decision string ("allow"/"deny"/...).
  bool applied;               ///< Reserved; currently always true.
};

/// Generic per-event payload passed to onEvent() for every parsed frame.
struct Event {
  const char* kind;             ///< event.kind (e.g. "activity.started").
  const char* agent;            ///< agent name from the envelope.
  const char* session_id;
  const char* turn_id;
  const char* activity_kind;    ///< Empty if not an activity event.
  const char* activity_tool;
  const char* activity_summary;
  JsonVariantConst event;       ///< The "event" subobject.
  JsonVariantConst envelope;    ///< The full envelope.
};

/// Catch-all for raw frames (any `type`, not only `agent_event`).
struct RawEvent {
  const char* type;             ///< Top-level "type" field.
  JsonVariantConst doc;         ///< Full JSON document.
};

/**
 * Singleton state snapshot. All renderers read this. Written only by
 * AgentEvents and the connection-state setters. Strings are
 * fixed-length char buffers so we never allocate at runtime; sizes are
 * sized for typical agent payloads with truncation as the fallback.
 *
 * Field groups:
 *  - **Connection**: wifi_connected / ws_connected / working.
 *  - **Identity**: agent / session_id / latched_session.
 *  - **Last seen**: last_event_kind / last_activity_kind / last_tool.
 *  - **Current activity**: current_tool / tool_detail; *_end_ms is
 *    the millis() at which the activity finished (0 = still running).
 *  - **Display strings**: status_line / subtitle_tool / body_text are
 *    consumed by TextScene.
 *  - **Per-turn context**: thinking_title_since_ms,
 *    turn_started_wall_ms, done_turn_elapsed_ms, latest_*_target,
 *    thought_lines, read/write_tools_this_turn.
 *  - **Pending permission**: pending_permission (request_id) +
 *    pending_tool / pending_detail.
 *  - **Render mode + linger window**.
 */
struct AgentState {
  bool wifi_connected;
  bool ws_connected;
  bool working;                 ///< True between turn.started and turn.ended.

  char agent[16];
  char session_id[40];
  char latched_session[40];     ///< @see AgentEvents.cpp::latchFilter.
  char last_event_kind[40];
  char last_activity_kind[24];
  char last_tool[32];

  char current_tool[32];        ///< Empty between activities.
  char tool_detail[80];
  uint32_t current_tool_end_ms; ///< 0 while running; millis() at finish.

  char last_summary[128];       ///< Most recent assistant/thinking text.
  char status_line[80];         ///< Header line in text mode (e.g. "Thinking", "Done").
  char subtitle_tool[320];      ///< Sub-header (current tool / activity summary).
  char body_text[512];          ///< Streaming body for thinking/assistant text.
  uint32_t thinking_title_since_ms;
  uint32_t turn_started_wall_ms;
  uint32_t done_turn_elapsed_ms;
  char latest_shell_command[160];
  char latest_read_target[160];
  char latest_write_target[160];
  char thought_lines[4][96];    ///< Reserved; filled by upstream consumers.
  uint8_t thought_count;
  uint32_t body_updated_ms;
  uint32_t thought_updated_ms;
  uint32_t text_tool_linger_until_ms;  ///< @see tick().
  RenderMode render_mode;

  uint16_t read_tools_this_turn;
  uint16_t write_tools_this_turn;

  char pending_permission[48];
  char pending_tool[32];
  char pending_detail[80];

  uint32_t last_event_ms;       ///< millis() of the most recent dispatch().
};

/// Read the singleton AgentState. Borrows; do not retain across loops.
const AgentState& state();

/// Reflect the current WiFi link state into AgentState.
void setWifiConnected(bool v);
/// Reflect the current bridge link state into AgentState.
void setWsConnected(bool v);

using PermissionRequestHandler = void (*)(const PermissionRequestEvent&);
using PermissionResolvedHandler = void (*)(const PermissionResolvedEvent&);
using EventHandler = void (*)(const Event&);
using RawHandler = void (*)(const RawEvent&);
using ConnectionHandler = void (*)(bool connected);

/// Register an edge handler for permission.requested. Single slot.
void onPermissionRequest(PermissionRequestHandler h);
/// Register an edge handler for permission.resolved. Single slot.
void onPermissionResolved(PermissionResolvedHandler h);
/// Register a handler that runs for every parsed agent_event frame.
void onEvent(EventHandler h);
/// Register a handler that runs for every raw bridge frame (any type).
void onRaw(RawHandler h);
/// Register a connection-state change handler. Fires from notifyConnection().
void onConnectionChange(ConnectionHandler h);

/**
 * Decide whether an activity is read-only or writes state. `file.write`,
 * `file.delete` and `notebook.edit` are always WRITE. `shell.exec` is
 * inspected for write-y command shapes (`>`, `tee`, `rm`, `mkdir`,
 * package-install commands, etc.). Everything else is READ.
 */
ActivityAccess classifyActivity(const char* activity_kind,
                                const char* activity_tool,
                                const char* activity_summary);

/// Current render mode (kept on AgentState as well for renderers).
RenderMode renderMode();
/// Set the render mode. Pure setter; persistence belongs to Settings.
void setRenderMode(RenderMode mode);

/// Zero out the AgentState and set RENDER_FACE. Call once in setup().
void begin();

/**
 * Top-level entry point: route a parsed bridge frame to the right
 * handler. Recognized `type` values: `agent_event` (the bulk), pre-flight
 * `active_sessions` (to maintain the latched session), `pong`, `error`.
 * Always invokes the registered RawHandler for transparency.
 */
void dispatch(JsonDocument& doc);

/**
 * Per-loop maintenance. Currently expires the text-tool linger window:
 * when an activity finished we hold its subtitle on screen for
 * `kTextToolLingerMs` (1 s) before falling back to the "Thinking"
 * header. Cheap; safe to call every loop.
 */
void tick();

/**
 * Invoked from the BridgeClient connection callback. Updates the WS
 * flag, clears all per-turn state on disconnect (since whatever the
 * agent was doing is now lost), and fans out to onConnectionChange.
 */
void notifyConnection(bool connected);

/**
 * Wipe the text-mode display strings and timing fields. Used by the
 * sleep state to stop showing stale activity while the screen is
 * dimmed/blanked.
 */
void clearTextDisplayForSleep();

}  // namespace AgentEvents
