#ifndef SULTANA_CORE_H
#define SULTANA_CORE_H

/*
 * SultanaCore: the robot_v3 firmware's behaviour, face and motion code,
 * compiled for a desktop host and driven through this C API.
 *
 * The host owns the WebSocket to the bridge and the clock. It feeds the core
 * the same text frames the robot receives, calls sultana_core_tick() often
 * (every display refresh is fine; the face throttles itself to the robot's
 * 33 ms / 16 ms cadence), and reads back a 240x240 RGB565 frame and the
 * commanded arm angle.
 *
 * Threading: single instance, not thread-safe. Make every call from one
 * thread.
 *
 * Time: every now_ms is the host's monotonic clock in milliseconds, from any
 * epoch (0 at start is fine). It must never go backwards. The core adds a
 * fixed offset before handing it to the firmware as millis(), because the
 * firmware uses 0 to mean "unset" in several places. Like the device's
 * millis(), it wraps after ~49.7 days.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SULTANA_SCREEN_WIDTH 240
#define SULTANA_SCREEN_HEIGHT 240
#define SULTANA_FRAME_RGBA_BYTES (SULTANA_SCREEN_WIDTH * SULTANA_SCREEN_HEIGHT * 4)

/* Outbound WebSocket text (today only {"type":"request_sessions"}). */
typedef void (*sultana_send_fn)(const char* json, void* user);
/* One firmware log line (LOG_INFO etc.), without the trailing newline. */
typedef void (*sultana_log_fn)(const char* line, void* user);

/*
 * The device's setup() minus WiFi, provisioning and bridge discovery:
 * Settings, Display, Motion, MotionBehaviors, Face, EventRouter, and the
 * bridge callbacks. Starts disconnected, asleep. May be called again to
 * reset behaviour (the in-memory settings and some animation phases survive).
 * rng_seed seeds random() and esp_random(); 0 is treated as 1.
 */
void sultana_core_init(uint32_t now_ms, uint32_t rng_seed);

void sultana_core_set_send_callback(sultana_send_fn fn, void* user);
/* NULL restores the default (stderr). */
void sultana_core_set_log_callback(sultana_log_fn fn, void* user);

/* Socket opened / closed. Same moment the device sees WStype_CONNECTED or
 * WStype_DISCONNECTED. Repeated calls with the same value are ignored. */
void sultana_core_set_connected(bool connected, uint32_t now_ms);

/* One received WebSocket text frame. Returns false if it was not valid JSON
 * (the device drops those too). The core always stays in face render mode,
 * whatever config_change frames say. */
bool sultana_core_inject(const char* json, size_t len, uint32_t now_ms);

/* One iteration of the device's loop(). Returns true if the face pushed a new
 * frame (it may be pixel-identical to the previous one). */
bool sultana_core_tick(uint32_t now_ms);

/* The last pushed frame: 240x240 RGB565, row-major, native byte order.
 * Valid until the next sultana_core_tick(). */
const uint16_t* sultana_core_frame565(void);
/* Number of frames pushed since init. */
uint32_t sultana_core_frame_count(void);
/* The last pushed frame as RGBA8888 (alpha 255), channels expanded from
 * 5/6 bits the way a panel shows them. out must hold
 * SULTANA_FRAME_RGBA_BYTES. */
void sultana_core_frame_rgba(uint8_t* out);

/* Arm offset in degrees the firmware last commanded the servo to
 * (Motion::currentOffsetDeg): rounded, clamped to the safe range (+-45).
 * The physical gear mirrors it: one arm at +theta, the other at -theta. */
int8_t sultana_core_arm_offset_deg(void);

/* Debug snapshot as a JSON object: verb, effective_verb, expression,
 * valence, activation, latched_session, pending_permission, read_tools,
 * write_tools, connected, arm_deg, millis. Returns the length written
 * (excluding the terminator), truncated to cap - 1. */
size_t sultana_core_state_json(char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* SULTANA_CORE_H */
