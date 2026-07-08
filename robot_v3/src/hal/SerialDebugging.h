#pragma once

/**
 * @file SerialDebugging.h
 * @brief Slash-command Serial console for runtime debugging.
 *
 * Polls `Serial` for newline-terminated commands and dispatches the
 * recognised ones. Commands are prefixed with `/` so they're easy to
 * pick out of log noise:
 *
 *  - `/reboot`             — `ESP.restart()`.
 *  - `/provision`          — set the one-shot portal flag and reboot,
 *                            so the next boot enters the captive AP
 *                            even if no button is held.
 *  - `/clear-provisioning` — wipe the entire `bridge_cfg` NVS namespace
 *                            (all remembered networks + legacy keys).
 *  - `/set-provisioning-address <url>` — re-point the bridge endpoint
 *                            (host + port) of the most-recent network
 *                            and the legacy keys, leaving SSID/password/
 *                            token untouched, then reboot. Accepts a URL
 *                            like `http://192.168.1.150:8787` (scheme and
 *                            path optional).
 *
 * Unknown commands log a warning and are otherwise ignored. Call
 * tick() from the main loop.
 */
namespace SerialDebugging {

/**
 * Display the start message on the display.
 */
void displayStartMessage();

/**
 * Drain any pending bytes from `Serial`, accumulating into a
 * line buffer. When a `\n` arrives, the assembled line is trimmed and
 * dispatched (the command token is lowercased; any argument keeps its
 * original case). Non-blocking — safe to call every loop.
 */
void tick();

}  // namespace SerialDebugging
