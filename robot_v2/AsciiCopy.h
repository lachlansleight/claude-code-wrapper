#pragma once

// String copy helpers that fold UTF-8 down to ASCII. The Adafruit GFX default
// font only renders ASCII, so anything coming out of Claude (smart quotes,
// em-dashes, arrows, emoji) needs to be transliterated before it hits the
// OLED. All state strings in ClaudeEvents and all tool details in ToolFormat
// are written through these helpers.

#include <Arduino.h>

namespace AsciiCopy {

// Copy `src` into `dst` (capped at `cap` including NUL), transliterating
// UTF-8 codepoints to ASCII. Common punctuation gets a sensible substitute
// ("—" → "--"); anything unknown becomes '?'. Newlines and tabs collapse to
// spaces; other control chars are dropped.
void copy(char* dst, size_t cap, const char* src);

// Same as `copy` but keeps `\n` / `\r` for multiline body text (tabs still → space).
void copyPreserveNewlines(char* dst, size_t cap, const char* src);

// Copy only the final path segment of `path`. Handles both '/' and '\\'
// separators. Passes through `copy()` so the result is ASCII-safe.
void basename(const char* path, char* out, size_t cap);

}  // namespace AsciiCopy
