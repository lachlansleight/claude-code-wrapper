#pragma once

#include <Arduino.h>

/**
 * @file DebugLog.h
 * @brief Tagged Serial logging macros used throughout the firmware.
 *
 * All firmware modules log via these macros so that output on the USB
 * serial console is easy to filter visually. Each macro wraps
 * `Serial.printf` and prepends a fixed-width tag plus a trailing newline,
 * so callers should pass a printf-style format string with no `\n`.
 *
 * Tag conventions:
 *  - `INFO ` / `WARN ` / `ERROR` — generic severity bands.
 *  - `ws   ` — WebSocket transport (BridgeClient).
 *  - `evt  ` — agent_event parsing / dispatch.
 *
 * These are unconditional — there is no compile-time gate. If silence
 * matters in production, redirect or disable `Serial` at the call site.
 */

/// Informational message. Use for normal lifecycle events.
#define LOG_INFO(fmt, ...)  Serial.printf("[INFO ] " fmt "\n", ##__VA_ARGS__)
/// Recoverable problem. Behaviour continues but something looked off.
#define LOG_WARN(fmt, ...)  Serial.printf("[WARN ] " fmt "\n", ##__VA_ARGS__)
/// Hard error. The action being attempted did not succeed.
#define LOG_ERR(fmt, ...)   Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
/// WebSocket transport trace (connect, disconnect, frame received).
#define LOG_WS(fmt, ...)    Serial.printf("[ws   ] " fmt "\n", ##__VA_ARGS__)
/// Agent event trace (parsed envelope kind, routing decisions).
#define LOG_EVT(fmt, ...)   Serial.printf("[evt  ] " fmt "\n", ##__VA_ARGS__)
