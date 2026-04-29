#include "AgentEvents.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "AsciiCopy.h"
#include "DebugLog.h"
#include "Motion.h"

namespace AgentEvents {

static constexpr uint32_t kTextToolLingerMs = 1000;

static AgentState g_state = {};

static PermissionRequestHandler   h_permReq = nullptr;
static PermissionResolvedHandler  h_permRes = nullptr;
static EventHandler               h_event   = nullptr;
static RawHandler                 h_raw     = nullptr;
static ConnectionHandler          h_conn    = nullptr;

const AgentState& state() { return g_state; }
RenderMode renderMode() { return g_state.render_mode; }
void setRenderMode(RenderMode mode) { g_state.render_mode = mode; }

void setWifiConnected(bool v) { g_state.wifi_connected = v; }
void setWsConnected(bool v)   { g_state.ws_connected   = v; }

void onPermissionRequest(PermissionRequestHandler h)  { h_permReq = h; }
void onPermissionResolved(PermissionResolvedHandler h){ h_permRes = h; }
void onEvent(EventHandler h)                          { h_event   = h; }
void onRaw(RawHandler h)                              { h_raw     = h; }
void onConnectionChange(ConnectionHandler h)          { h_conn    = h; }

void clearTextDisplayForSleep() {
  g_state.status_line[0] = '\0';
  g_state.subtitle_tool[0] = '\0';
  g_state.body_text[0] = '\0';
  g_state.last_summary[0] = '\0';
  g_state.thinking_title_since_ms = 0;
  g_state.turn_started_wall_ms = 0;
  g_state.done_turn_elapsed_ms = 0;
  g_state.text_tool_linger_until_ms = 0;
}

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
    g_state.status_line[0] = '\0';
    g_state.subtitle_tool[0] = '\0';
    g_state.body_text[0] = '\0';
    g_state.thinking_title_since_ms = 0;
    g_state.turn_started_wall_ms = 0;
    g_state.done_turn_elapsed_ms = 0;
    g_state.text_tool_linger_until_ms = 0;
    g_state.latest_shell_command[0] = '\0';
    g_state.latest_read_target[0] = '\0';
    g_state.latest_write_target[0] = '\0';
    g_state.thought_count = 0;
  }
  if (h_conn) h_conn(connected);
}

static bool startsWithWord(const char* text, const char* word) {
  if (!text || !word) return false;
  const size_t n = strlen(word);
  if (strncmp(text, word, n) != 0) return false;
  const char c = text[n];
  return c == '\0' || c == ' ' || c == '\t';
}

static bool containsToken(const char* text, const char* token) {
  return text && token && strstr(text, token) != nullptr;
}

static bool containsText(const char* text, const char* needle) {
  if (!text || !needle) return false;
  return strstr(text, needle) != nullptr;
}

static void lowerCopy(const char* in, char* out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (!in || !*in) return;
  size_t i = 0;
  for (; i + 1 < cap && in[i]; ++i) {
    out[i] = (char)tolower((unsigned char)in[i]);
  }
  out[i] = '\0';
}

static bool shellLikelyWrites(const char* summary) {
  if (!summary || !*summary) return false;
  char cmd[128];
  lowerCopy(summary, cmd, sizeof(cmd));

  // Redirections and tee generally write to files.
  if (containsToken(cmd, ">>") ||
      containsToken(cmd, " >") ||
      startsWithWord(cmd, ">") ||
      containsToken(cmd, "tee ")) return true;

  // Explicit file-mutating command families.
  const char* kWriteCmds[] = {
    "touch", "mkdir", "rmdir", "rm", "mv", "cp", "install", "truncate",
    "chmod", "chown", "ln", "dd", "sed -i", "perl -i",
    "npm install", "npm uninstall", "pnpm add", "pnpm remove",
    "yarn add", "yarn remove", "bun add", "bun remove",
    "pip install", "pip uninstall",
  };
  for (size_t i = 0; i < sizeof(kWriteCmds) / sizeof(kWriteCmds[0]); ++i) {
    if (startsWithWord(cmd, kWriteCmds[i])) return true;
  }

  return false;
}

static void copyStreamBody(const char* src) {
  AsciiCopy::copyPreserveNewlines(g_state.body_text, sizeof(g_state.body_text),
                                  src ? src : "");
  g_state.body_updated_ms = millis();
}

static void copySubtitle(const char* src) {
  AsciiCopy::copy(g_state.subtitle_tool, sizeof(g_state.subtitle_tool), src ? src : "");
}

/** Subtitle when title is Executing: `Shell: command…` (not used for Reading/Writing). */
static void copyExecutingToolSubtitle(const char* tool, const char* detail) {
  const bool haveTool = tool && *tool;
  const bool haveDet = detail && *detail;
  char line[sizeof(g_state.subtitle_tool)];
  if (haveTool && haveDet) {
    snprintf(line, sizeof(line), "%s: %s", tool, detail);
  } else if (haveTool) {
    AsciiCopy::copy(line, sizeof(line), tool);
  } else {
    AsciiCopy::copy(line, sizeof(line), haveDet ? detail : "");
  }
  copySubtitle(line);
}

static void setThinkingTitle() {
  AsciiCopy::copy(g_state.status_line, sizeof(g_state.status_line), "Thinking");
  g_state.thinking_title_since_ms = millis();
}

static void formatByteSize(char* out, size_t cap, uint32_t bytes) {
  if (!out || cap < 8) {
    if (out && cap) out[0] = '\0';
    return;
  }
  if (bytes < 1000u) {
    snprintf(out, cap, "%lu B", (unsigned long)bytes);
    return;
  }
  if (bytes >= 1000000u) {
    const double mb = (double)bytes / 1000000.0;
    snprintf(out, cap, "%.3gMB", mb);
    return;
  }
  const double kb = (double)bytes / 1000.0;
  snprintf(out, cap, "%.3gkB", kb);
}

/** Map activity.kind to the short title line (text mode + personality hints). */
static const char* titleForToolActivity(const char* activity_kind) {
  if (!activity_kind || !*activity_kind) return "Reading";
  if (!strcmp(activity_kind, "shell.exec") || !strcmp(activity_kind, "shell.background")) {
    return "Executing";
  }
  if (!strcmp(activity_kind, "file.write") || !strcmp(activity_kind, "file.delete")) {
    return "Writing";
  }
  if (!strncmp(activity_kind, "file.", 5) || !strcmp(activity_kind, "notebook.edit")) {
    return "Reading";
  }
  return "Executing";
}

static void armTextToolLinger() {
  const uint32_t t = millis();
  g_state.text_tool_linger_until_ms = t + kTextToolLingerMs;
  if (g_state.text_tool_linger_until_ms < t) g_state.text_tool_linger_until_ms = (uint32_t)-1;
}

static void clearTextToolLinger() { g_state.text_tool_linger_until_ms = 0; }

static bool fileActivityWantsByteSuffix(const char* activity_kind) {
  return activity_kind &&
         (!strcmp(activity_kind, "file.read") || !strcmp(activity_kind, "file.write"));
}

static void deriveTargets(const char* activity_kind, const char* summary) {
  if (!summary || !*summary) return;
  if (activity_kind && containsText(activity_kind, "shell.exec")) {
    AsciiCopy::copy(g_state.latest_shell_command, sizeof(g_state.latest_shell_command),
                    summary);
    return;
  }
  if (activity_kind && containsText(activity_kind, "file.read")) {
    AsciiCopy::copy(g_state.latest_read_target, sizeof(g_state.latest_read_target), summary);
    return;
  }
  if (activity_kind && containsText(activity_kind, "file.write")) {
    AsciiCopy::copy(g_state.latest_write_target, sizeof(g_state.latest_write_target),
                    summary);
    return;
  }
  if (containsText(summary, "read ") || containsText(summary, "Read ")) {
    AsciiCopy::copy(g_state.latest_read_target, sizeof(g_state.latest_read_target), summary);
  } else if (containsText(summary, "write ") || containsText(summary, "Write ")) {
    AsciiCopy::copy(g_state.latest_write_target, sizeof(g_state.latest_write_target),
                    summary);
  }
}

static void applyConfigChange(JsonDocument& doc) {
  JsonVariantConst modeVar = doc["display_mode"];
  if (modeVar.isNull()) {
    modeVar = doc["config"]["display_mode"];
  }
  const char* mode = modeVar | "";
  if (!mode || !*mode) return;
  if (!strcmp(mode, "text")) {
    g_state.render_mode = RENDER_TEXT;
  } else if (!strcmp(mode, "face")) {
    g_state.render_mode = RENDER_FACE;
  }
}

ActivityAccess classifyActivity(const char* activity_kind,
                                const char* activity_tool,
                                const char* activity_summary) {
  (void)activity_tool;
  if (!activity_kind || !*activity_kind) return ACTIVITY_READ;

  if (!strcmp(activity_kind, "file.write") ||
      !strcmp(activity_kind, "file.delete") ||
      !strcmp(activity_kind, "notebook.edit")) {
    return ACTIVITY_WRITE;
  }

  if (!strcmp(activity_kind, "shell.exec")) {
    return shellLikelyWrites(activity_summary) ? ACTIVITY_WRITE : ACTIVITY_READ;
  }

  return ACTIVITY_READ;
}

static bool latchFilter(const char* session_id, const char* event_kind) {
  if (!session_id || !*session_id) {
    // Once latched, ignore unscoped events so stray adapter traffic
    // (or malformed envelopes) can't overwrite the active session state.
    return g_state.latched_session[0] == '\0';
  }

  if (g_state.latched_session[0] == '\0') {
    AsciiCopy::copy(g_state.latched_session, sizeof(g_state.latched_session), session_id);
    LOG_EVT("session latched via event: %s", session_id);
    return true;
  }

  if (strcmp(g_state.latched_session, session_id) != 0) {
    // If a new foreground turn/session starts on a different session id,
    // follow it immediately instead of waiting for stale-session cleanup.
    if (event_kind &&
        (!strcmp(event_kind, "turn.started") || !strcmp(event_kind, "session.started"))) {
      LOG_EVT("session relatched via event: %s -> %s",
              g_state.latched_session, session_id);
      AsciiCopy::copy(g_state.latched_session, sizeof(g_state.latched_session), session_id);
      return true;
    }
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

  if (!strcmp(kind, "message.user")) {
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
    return;
  }

  if (!strcmp(kind, "turn.started")) {
    g_state.working = true;
    clearTextToolLinger();
    setThinkingTitle();
    g_state.turn_started_wall_ms = millis();
    g_state.done_turn_elapsed_ms = 0;
    g_state.subtitle_tool[0] = '\0';
    g_state.current_tool[0] = '\0';
    g_state.tool_detail[0] = '\0';
    g_state.current_tool_end_ms = 0;
    g_state.read_tools_this_turn = 0;
    g_state.write_tools_this_turn = 0;
    g_state.last_summary[0] = '\0';
    g_state.body_text[0] = '\0';
    g_state.body_updated_ms = millis();
    g_state.latest_shell_command[0] = '\0';
    g_state.latest_read_target[0] = '\0';
    g_state.latest_write_target[0] = '\0';
    g_state.thought_count = 0;
    // A new turn starting means any prior pending permission is no longer
    // blocking — clear it so Personality can leave BLOCKED. Bridge can't
    // reliably relay permission.resolved, so this is our recovery path.
    g_state.pending_permission[0] = '\0';
    g_state.pending_tool[0] = '\0';
    g_state.pending_detail[0] = '\0';
  } else if (!strcmp(kind, "turn.ended") || !strcmp(kind, "session.ended")) {
    g_state.working = false;
    g_state.current_tool[0] = '\0';
    g_state.tool_detail[0] = '\0';
    g_state.current_tool_end_ms = 0;
    g_state.pending_permission[0] = '\0';
    g_state.pending_tool[0] = '\0';
    g_state.pending_detail[0] = '\0';
    if (!strcmp(kind, "session.ended")) {
      g_state.read_tools_this_turn = 0;
      g_state.write_tools_this_turn = 0;
    }
  } else if (!strcmp(kind, "activity.started")) {
    g_state.working = true;
    clearTextToolLinger();
    AsciiCopy::copy(g_state.current_tool, sizeof(g_state.current_tool), activity_tool);
    AsciiCopy::copy(g_state.tool_detail, sizeof(g_state.tool_detail), activity_summary);
    deriveTargets(activity_kind, activity_summary);
    const char* actTitle = titleForToolActivity(activity_kind);
    AsciiCopy::copy(g_state.status_line, sizeof(g_state.status_line), actTitle);
    if (!strcmp(actTitle, "Executing")) {
      copyExecutingToolSubtitle(activity_tool, activity_summary);
    } else {
      copySubtitle(activity_summary);
    }
    g_state.current_tool_end_ms = 0;
    g_state.last_summary[0] = '\0';
  } else if (!strcmp(kind, "activity.finished") || !strcmp(kind, "activity.failed")) {
    g_state.current_tool_end_ms = millis();
    if (g_state.current_tool_end_ms == 0) g_state.current_tool_end_ms = 1;
    if (classifyActivity(activity_kind, activity_tool, activity_summary) == ACTIVITY_WRITE) {
      if (g_state.write_tools_this_turn < UINT16_MAX) g_state.write_tools_this_turn++;
    } else {
      if (g_state.read_tools_this_turn < UINT16_MAX) g_state.read_tools_this_turn++;
    }

    clearTextToolLinger();
    const char* actTitle = titleForToolActivity(activity_kind);
    AsciiCopy::copy(g_state.status_line, sizeof(g_state.status_line), actTitle);

    JsonVariantConst clv = evt["content_length"];
    uint32_t content_len = 0;
    bool have_len = false;
    if (!clv.isNull()) {
      const int v = clv.as<int>();
      if (v >= 0) {
        content_len = (uint32_t)v;
        have_len = true;
      }
    }

    char sizeBuf[28];
    sizeBuf[0] = '\0';
    if (have_len && fileActivityWantsByteSuffix(activity_kind)) {
      formatByteSize(sizeBuf, sizeof(sizeBuf), content_len);
    }

    if (!strcmp(actTitle, "Executing")) {
      if (sizeBuf[0] && activity_summary && *activity_summary) {
        char detail[sizeof(g_state.subtitle_tool)];
        snprintf(detail, sizeof(detail), "%s - %s", activity_summary, sizeBuf);
        copyExecutingToolSubtitle(activity_tool, detail);
      } else if (sizeBuf[0]) {
        copyExecutingToolSubtitle(activity_tool, sizeBuf);
      } else {
        copyExecutingToolSubtitle(activity_tool, activity_summary);
      }
    } else {
      if (sizeBuf[0] && activity_summary && *activity_summary) {
        char line[sizeof(g_state.subtitle_tool)];
        snprintf(line, sizeof(line), "%s - %s", activity_summary, sizeBuf);
        copySubtitle(line);
      } else if (sizeBuf[0]) {
        copySubtitle(sizeBuf);
      } else {
        copySubtitle(activity_summary);
      }
    }

    armTextToolLinger();
  } else if (!strcmp(kind, "permission.requested")) {
    const char* request_id = evt["request_id"] | "";
    AsciiCopy::copy(g_state.pending_permission, sizeof(g_state.pending_permission), request_id);
    AsciiCopy::copy(g_state.pending_tool, sizeof(g_state.pending_tool), activity_tool);
    AsciiCopy::copy(g_state.pending_detail, sizeof(g_state.pending_detail), activity_summary);

    clearTextToolLinger();
    AsciiCopy::copy(g_state.status_line, sizeof(g_state.status_line), "Awaiting permission");
    const char* desc = evt["description"] | "";
    if (activity_summary && *activity_summary) {
      copySubtitle(activity_summary);
    } else if (desc && *desc) {
      copySubtitle(desc);
    } else {
      copySubtitle("");
    }

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
    clearTextToolLinger();
    if (strcmp(g_state.status_line, "Done") != 0) {
      const uint32_t wall = millis();
      const uint32_t t0 = g_state.turn_started_wall_ms;
      g_state.done_turn_elapsed_ms =
          (t0 != 0u && wall >= t0) ? (uint32_t)(wall - t0) : 0u;
    }
    AsciiCopy::copy(g_state.status_line, sizeof(g_state.status_line), "Done");
    g_state.subtitle_tool[0] = '\0';
    copyStreamBody(evt["text"] | "");
  } else if (!strcmp(kind, "notification")) {
    AsciiCopy::copy(g_state.last_summary, sizeof(g_state.last_summary), evt["text"] | "");
    clearTextToolLinger();
    copyStreamBody(evt["text"] | "");
  } else if (!strcmp(kind, "thinking")) {
    AsciiCopy::copy(g_state.last_summary, sizeof(g_state.last_summary), evt["text"] | "");
    clearTextToolLinger();
    setThinkingTitle();
    copyStreamBody(evt["text"] | "");
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

void tick() {
  const uint32_t now = millis();
  const uint32_t until = g_state.text_tool_linger_until_ms;
  if (until == 0) return;
  if ((int32_t)(now - until) < 0) return;

  g_state.text_tool_linger_until_ms = 0;
  if (strcmp(g_state.status_line, "Done") != 0) {
    setThinkingTitle();
    g_state.subtitle_tool[0] = '\0';
  }
}

void dispatch(JsonDocument& doc) {
  g_state.last_event_ms = millis();
  const char* type = doc["type"] | "";
  if (!type || !*type) return;

  if (!strcmp(type, "agent_event")) {
    handleAgentEvent(doc);
  } else if (!strcmp(type, "config_change")) {
    applyConfigChange(doc);
  } else if (!strcmp(type, "set_servo_position")) {
    JsonVariantConst posVar = doc["position"];
    JsonVariantConst durVar = doc["duration_ms"];
    if (!posVar.isNull()) {
      const int pos = posVar.as<int>();
      const uint32_t dur = durVar.isNull() ? 5000u : (uint32_t)durVar.as<int>();
      int clamped = pos;
      if (clamped < -90) clamped = -90;
      if (clamped >  90) clamped =  90;
      LOG_EVT("set_servo_position pos=%d dur=%lu", clamped, (unsigned long)dur);
      Motion::holdPosition((int8_t)clamped, dur);
    }
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
