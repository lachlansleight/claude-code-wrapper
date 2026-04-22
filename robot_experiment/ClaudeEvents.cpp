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

// Decode one UTF-8 codepoint; advances `*p` past the sequence. Returns
// UINT32_MAX on malformed input (advancing one byte) so the caller can emit
// a single replacement without desyncing.
static uint32_t utf8Decode(const char** p) {
  const uint8_t b0 = (uint8_t)**p;
  if (b0 < 0x80) { (*p)++; return b0; }
  uint32_t cp;
  int extra;
  if      ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; extra = 1; }
  else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; extra = 2; }
  else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; extra = 3; }
  else { (*p)++; return UINT32_MAX; }
  (*p)++;
  for (int i = 0; i < extra; ++i) {
    const uint8_t b = (uint8_t)**p;
    if ((b & 0xC0) != 0x80) return UINT32_MAX;
    cp = (cp << 6) | (b & 0x3F);
    (*p)++;
  }
  return cp;
}

// ASCII substitute for common Unicode chars Claude tends to emit. Returns
// nullptr if we don't have a mapping; callers fall back to '?'.
static const char* asciiSubstitute(uint32_t cp) {
  switch (cp) {
    case 0x2013: return "-";   case 0x2014: return "--";  // en/em dash
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: return "\"";
    case 0x2026: return "...";                            // ellipsis
    case 0x2022: case 0x00B7: case 0x2219: return "*";    // bullet / mid-dot
    case 0x2190: return "<-";  case 0x2192: return "->";
    case 0x2191: return "^";   case 0x2193: return "v";
    case 0x00A0: return " ";                              // nbsp
    case 0x00AB: return "<<";  case 0x00BB: return ">>";
    case 0x2713: case 0x2714: return "v";                 // check
    case 0x2717: case 0x2718: case 0x2715: return "x";    // cross
    case 0x00A9: return "(c)";
    case 0x00AE: return "(R)";
    case 0x2122: return "(TM)";
    case 0x00B0: return "deg";
    case 0x00B1: return "+/-";
    case 0x00D7: return "x";   case 0x00F7: return "/";
    default:     return nullptr;
  }
}

// Copy `src` into `dst` while folding UTF-8 / control chars down to ASCII
// the Adafruit GFX default font can render. Newlines collapse to spaces;
// unknown non-ASCII codepoints become '?'.
static void copyStr(char* dst, size_t cap, const char* src) {
  if (!dst || cap == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  size_t o = 0;
  const char* p = src;
  while (*p && o + 1 < cap) {
    const uint32_t cp = utf8Decode(&p);
    if (cp < 0x80) {
      char c = (char)cp;
      if (c == '\n' || c == '\r' || c == '\t') c = ' ';
      if ((unsigned char)c < 0x20) continue;  // drop other control chars
      dst[o++] = c;
    } else {
      const char* rep = asciiSubstitute(cp);
      if (!rep) rep = "?";
      while (*rep && o + 1 < cap) dst[o++] = *rep++;
    }
  }
  dst[o] = '\0';
}

// Strip directory prefix from a file path. Handles both / and \ separators.
static void basename(const char* path, char* out, size_t cap) {
  if (!path) { out[0] = '\0'; return; }
  const char* slash = nullptr;
  for (const char* p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') slash = p;
  }
  copyStr(out, cap, slash ? slash + 1 : path);
}

// Best-effort short description of a tool call, sourced from its input blob.
// Fields mirror Claude Code's tool schemas; see TOOL_DISPLAY.md for the full
// catalogue and how to customize per tool.
static void formatToolDetail(const char* tool, JsonVariantConst input,
                             char* out, size_t cap) {
  out[0] = '\0';
  if (!tool || !*tool || input.isNull()) return;

  if (!strcmp(tool, "Edit") || !strcmp(tool, "Write") ||
      !strcmp(tool, "Read") || !strcmp(tool, "MultiEdit")) {
    basename(input["file_path"] | "", out, cap);
  } else if (!strcmp(tool, "NotebookEdit")) {
    basename(input["notebook_path"] | "", out, cap);
  } else if (!strcmp(tool, "Bash")) {
    copyStr(out, cap, input["command"] | "");
  } else if (!strcmp(tool, "BashOutput")) {
    copyStr(out, cap, input["bash_id"] | "");
  } else if (!strcmp(tool, "KillShell")) {
    copyStr(out, cap, input["shell_id"] | "");
  } else if (!strcmp(tool, "Glob") || !strcmp(tool, "Grep")) {
    copyStr(out, cap, input["pattern"] | "");
  } else if (!strcmp(tool, "WebFetch")) {
    const char* url = input["url"] | "";
    const char* p = strstr(url, "://");
    copyStr(out, cap, p ? p + 3 : url);
  } else if (!strcmp(tool, "WebSearch")) {
    copyStr(out, cap, input["query"] | "");
  } else if (!strcmp(tool, "Task")) {
    copyStr(out, cap, input["subagent_type"] | "");
  } else if (!strcmp(tool, "TodoWrite")) {
    JsonVariantConst todos = input["todos"];
    if (todos.is<JsonArrayConst>()) {
      snprintf(out, cap, "%u items", (unsigned)todos.size());
    }
  } else if (!strcmp(tool, "SlashCommand")) {
    copyStr(out, cap, input["command"] | "");
  } else if (!strncmp(tool, "mcp__", 5)) {
    // Drop the "mcp__" prefix; keeps the server__tool portion.
    copyStr(out, cap, tool + 5);
  }
}

// Grab the last assistant text block from a hook payload (hook-forward.ts
// attaches `assistant_text` as a string array on PostToolUse / Stop).
static void captureAssistantSummary(JsonVariantConst payload,
                                    char* out, size_t cap) {
  JsonVariantConst arr = payload["assistant_text"];
  if (!arr.is<JsonArrayConst>()) return;
  size_t n = arr.size();
  if (n == 0) return;
  const char* last = arr[n - 1] | "";
  if (*last) copyStr(out, cap, last);
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
    copyStr(g_state.current_tool, sizeof(g_state.current_tool), evt.tool_name);
    formatToolDetail(evt.tool_name, input,
                     g_state.tool_detail, sizeof(g_state.tool_detail));
    g_state.current_tool_end_ms = 0;
  } else if (!strcmp(h, "PostToolUse")) {
    g_state.current_tool_end_ms = millis();
    if (g_state.current_tool_end_ms == 0) g_state.current_tool_end_ms = 1;
  }

  // Capture the most recent assistant snippet for the idle view.
  captureAssistantSummary(evt.payload,
                          g_state.last_summary, sizeof(g_state.last_summary));
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
    copyStr(g_state.last_summary, sizeof(g_state.last_summary), e.content);
    // New displayable content — retire any lingering tool label.
    g_state.current_tool[0] = '\0';
    g_state.tool_detail[0] = '\0';
    g_state.current_tool_end_ms = 0;
    LOG_EVT("inbound chat=%s \"%.40s\"", e.chat_id, e.content);
    if (h_inbound) h_inbound(e);
  }
  else if (!strcmp(type, "outbound_reply")) {
    OutboundReplyEvent e = {
      doc["content"] | "",
      doc["chat_id"] | "",
    };
    copyStr(g_state.last_summary, sizeof(g_state.last_summary), e.content);
    g_state.current_tool[0] = '\0';
    g_state.tool_detail[0] = '\0';
    g_state.current_tool_end_ms = 0;
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
    formatToolDetail(e.tool_name, e.input,
                     g_state.pending_detail, sizeof(g_state.pending_detail));
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
      g_state.pending_detail[0] = '\0';
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
