#pragma once

// Rendering helpers for Claude Code tool-use events.
//
// One module owns both the short upper-case **label** (EDIT, BASH, GREP, ...)
// and the per-tool **detail** string (filename, command, pattern, ...). Keep
// them together so customizing a tool's on-screen appearance is a single
// file edit — see TOOL_DISPLAY.md for the catalogue and budget guidance.

#include <Arduino.h>
#include <ArduinoJson.h>

namespace ToolFormat {

// Short upper-case label for a tool name, e.g. "Edit" → "EDIT".
// Falls back to the raw tool name if unknown. Always non-null.
const char* label(const char* tool);

// Writes a best-effort one-line description of a tool call into `out`,
// sourced from the tool's `tool_input` blob. Empty string if unknown tool,
// null input, or no useful field.
void detail(const char* tool, JsonVariantConst input, char* out, size_t cap);

}  // namespace ToolFormat
