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
 *
 * Unknown commands log a warning and are otherwise ignored. Call
 * tick() from the main loop.
 */
namespace SerialDebugging {

/**
 * Drain any pending bytes from `Serial`, accumulating into a
 * line buffer. When a `\n` arrives, the assembled line is trimmed,
 * lowercased, and dispatched. Non-blocking — safe to call every loop.
 */
void tick();

}  // namespace SerialDebugging
