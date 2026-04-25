#include "ToolFormat.h"

#include <string.h>

#include "AsciiCopy.h"

namespace ToolFormat {

ToolAccess access(const char* tool) {
  if (!tool || !*tool) return ACCESS_READ;

  // Legacy Claude tool names.
  if (!strcmp(tool, "Write") ||
      !strcmp(tool, "Edit") ||
      !strcmp(tool, "MultiEdit") ||
      !strcmp(tool, "NotebookEdit") ||
      !strcmp(tool, "TodoWrite") ||
      !strcmp(tool, "Bash")) return ACCESS_WRITE;

  // Cursor/Codex tool names.
  if (!strcmp(tool, "ApplyPatch") ||
      !strcmp(tool, "Delete") ||
      !strcmp(tool, "EditNotebook") ||
      !strcmp(tool, "GenerateImage") ||
      !strcmp(tool, "Shell") ||
      !strcmp(tool, "CallMcpTool") ||
      !strcmp(tool, "Subagent")) return ACCESS_WRITE;

  // Read-only tools and unknowns default to read.
  return ACCESS_READ;
}

const char* label(const char* tool) {
  if (!tool || !*tool)               return "";
  if (!strcmp(tool, "Edit"))         return "EDIT";
  if (!strcmp(tool, "MultiEdit"))    return "MEDIT";
  if (!strcmp(tool, "Write"))        return "WRITE";
  if (!strcmp(tool, "Read"))         return "READ";
  if (!strcmp(tool, "NotebookEdit")) return "NBEDT";
  if (!strcmp(tool, "Bash"))         return "BASH";
  if (!strcmp(tool, "BashOutput"))   return "SHOUT";
  if (!strcmp(tool, "KillShell"))    return "KILL";
  if (!strcmp(tool, "Glob"))         return "GLOB";
  if (!strcmp(tool, "Grep"))         return "GREP";
  if (!strcmp(tool, "WebFetch"))     return "FETCH";
  if (!strcmp(tool, "WebSearch"))    return "SEARCH";
  if (!strcmp(tool, "Task"))         return "TASK";
  if (!strcmp(tool, "TodoWrite"))    return "TODOS";
  if (!strcmp(tool, "SlashCommand")) return "CMD";
  if (!strcmp(tool, "ExitPlanMode")) return "PLAN";
  if (!strncmp(tool, "mcp__", 5))    return "MCP";
  return tool;
}

void detail(const char* tool, JsonVariantConst input, char* out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (!tool || !*tool || input.isNull()) return;

  if (!strcmp(tool, "Edit") || !strcmp(tool, "Write") ||
      !strcmp(tool, "Read") || !strcmp(tool, "MultiEdit")) {
    AsciiCopy::basename(input["file_path"] | "", out, cap);
  } else if (!strcmp(tool, "NotebookEdit")) {
    AsciiCopy::basename(input["notebook_path"] | "", out, cap);
  } else if (!strcmp(tool, "Bash")) {
    AsciiCopy::copy(out, cap, input["command"] | "");
  } else if (!strcmp(tool, "BashOutput")) {
    AsciiCopy::copy(out, cap, input["bash_id"] | "");
  } else if (!strcmp(tool, "KillShell")) {
    AsciiCopy::copy(out, cap, input["shell_id"] | "");
  } else if (!strcmp(tool, "Glob") || !strcmp(tool, "Grep")) {
    AsciiCopy::copy(out, cap, input["pattern"] | "");
  } else if (!strcmp(tool, "WebFetch")) {
    const char* url = input["url"] | "";
    const char* p = strstr(url, "://");
    AsciiCopy::copy(out, cap, p ? p + 3 : url);
  } else if (!strcmp(tool, "WebSearch")) {
    AsciiCopy::copy(out, cap, input["query"] | "");
  } else if (!strcmp(tool, "Task")) {
    AsciiCopy::copy(out, cap, input["subagent_type"] | "");
  } else if (!strcmp(tool, "TodoWrite")) {
    JsonVariantConst todos = input["todos"];
    if (todos.is<JsonArrayConst>()) {
      snprintf(out, cap, "%u items", (unsigned)todos.size());
    }
  } else if (!strcmp(tool, "SlashCommand")) {
    AsciiCopy::copy(out, cap, input["command"] | "");
  } else if (!strncmp(tool, "mcp__", 5)) {
    AsciiCopy::copy(out, cap, tool + 5);
  }
}

}  // namespace ToolFormat
