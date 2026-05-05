#pragma once

#include <Arduino.h>

/**
 * @file AsciiCopy.h
 * @brief UTF-8 → display-safe ASCII string sanitizer.
 *
 * The TFT_eSPI bitmap fonts used on the round display only carry glyphs
 * for printable 7-bit ASCII. Strings coming from agent payloads are
 * UTF-8 and frequently contain "smart" punctuation (curly quotes, em
 * dashes, ellipses, arrows, check/cross marks) that would render as
 * tofu or random glyphs.
 *
 * This namespace exposes a small family of fixed-buffer copy routines
 * that decode UTF-8 codepoints, substitute a curated list of common
 * non-ASCII characters with sensible ASCII equivalents (e.g. `…` →
 * `...`, `→` → `->`, `°` → `deg`), drop unprintable control bytes, and
 * truncate cleanly to fit the destination buffer.
 *
 * **All strings destined for the display must pass through here.**
 * Anything from the WebSocket or NVS may carry UTF-8.
 */
namespace AsciiCopy {

/**
 * Sanitize @p src into the fixed buffer @p dst (capacity @p cap, including
 * the null terminator). Tabs, newlines and carriage returns are folded to
 * spaces; other control bytes are dropped; high codepoints are replaced
 * with curated ASCII equivalents (or `?` if unknown). Always
 * null-terminates as long as `cap > 0`. No-op if `dst` is null or
 * `cap == 0`.
 */
void copy(char* dst, size_t cap, const char* src);

/**
 * Same as copy() but preserves `\n` and `\r` so the caller can do its
 * own line wrapping. Use this for multi-line text scenes; use copy()
 * for single-line labels.
 */
void copyPreserveNewlines(char* dst, size_t cap, const char* src);

/**
 * Strip everything up to and including the final `/` or `\\` in @p path
 * and copy the remainder into @p out via copy() (so the result is also
 * sanitized). Used to display file paths from tool calls without the
 * full directory chain.
 */
void basename(const char* path, char* out, size_t cap);

}  // namespace AsciiCopy
