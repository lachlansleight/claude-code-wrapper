#pragma once

// Rendering helpers for Claude Code tool-use events.
//
// One module owns both the short upper-case **label** (EDIT, BASH, GREP, ...)
// and the per-tool **detail** string (filename, command, pattern, ...). Keep
// them together so customizing a tool's on-screen appearance is a single
// file edit. Currently unused by the face renderer — kept for future overlays.

#include <Arduino.h>
#include <ArduinoJson.h>

namespace ToolFormat {

enum ToolAccess : uint8_t {
  ACCESS_READ = 0,
  ACCESS_WRITE,
};

// Short upper-case label for a tool name, e.g. "Edit" → "EDIT".
// Falls back to the raw tool name if unknown. Always non-null.
const char* label(const char* tool);

// Categorizes every tool into read-vs-write based on whether it can modify
// on-disk data. Unknown tools default to read.
ToolAccess access(const char* tool);

// Writes a best-effort one-line description of a tool call into `out`,
// sourced from the tool's `tool_input` blob. Empty string if unknown tool,
// null input, or no useful field.
void detail(const char* tool, JsonVariantConst input, char* out, size_t cap);

}  // namespace ToolFormat
