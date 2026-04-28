#include "AsciiCopy.h"

namespace AsciiCopy {

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

static void copyCore(char* dst, size_t cap, const char* src, bool preserve_newlines) {
  if (!dst || cap == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t o = 0;
  const char* p = src;
  while (*p && o + 1 < cap) {
    const uint32_t cp = utf8Decode(&p);
    if (cp < 0x80) {
      char c = (char)cp;
      if (preserve_newlines && (c == '\n' || c == '\r')) {
        dst[o++] = c;
      } else {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if ((unsigned char)c < 0x20) continue;  // drop other control chars
        dst[o++] = c;
      }
    } else {
      const char* rep = asciiSubstitute(cp);
      if (!rep) rep = "?";
      while (*rep && o + 1 < cap) dst[o++] = *rep++;
    }
  }
  dst[o] = '\0';
}

void copy(char* dst, size_t cap, const char* src) { copyCore(dst, cap, src, false); }

void copyPreserveNewlines(char* dst, size_t cap, const char* src) {
  copyCore(dst, cap, src, true);
}

void basename(const char* path, char* out, size_t cap) {
  if (!path) { if (out && cap) out[0] = '\0'; return; }
  const char* slash = nullptr;
  for (const char* p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') slash = p;
  }
  copy(out, cap, slash ? slash + 1 : path);
}

}  // namespace AsciiCopy
