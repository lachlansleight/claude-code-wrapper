#pragma once

#include <Arduino.h>

/**
 * @file BridgeDiscovery.h
 * @brief Boot-time search for the bridge server on the local network.
 *
 * The bridge machine's IP tends to move (DHCP), so instead of trusting a
 * fixed address the firmware looks for the bridge at boot:
 *
 *  1. Probe the **last known good** host (the address stored by
 *     Provisioning for the current network).
 *  2. If that fails, sweep every host `.1`–`.254` of the robot's own
 *     /24 on the same port.
 *
 * A host only counts as the bridge if it accepts a TCP connection *and*
 * answers `GET /api/health` with `{"ok":true,...}` — that endpoint is
 * unauthenticated, so no token is needed and an unrelated service that
 * happens to listen on the same port is not mistaken for the bridge.
 *
 * The sweep keeps several non-blocking connects in flight at once: dead
 * addresses can only fail by timing out, so probing them one at a time
 * would take minutes. The in-flight count is deliberately kept below
 * lwIP's ARP table size (10) so a pending probe's ARP entry is never
 * recycled before its reply arrives.
 *
 * Everything here **blocks** and is meant to be called from `setup()`
 * only, after WiFi is up and before Bridge::begin().
 */
namespace BridgeDiscovery {

/// Number of hosts covered by a full sweep (`.1` through `.254`).
static constexpr uint16_t kSweepHosts = 254;

/**
 * Called as the sweep advances. @p scanned counts hosts that have been
 * ruled out so far, out of @p total. Invoked at most once per poll
 * cycle, and only when @p scanned has changed.
 */
using ProgressHandler = void (*)(uint16_t scanned, uint16_t total);

/**
 * Find the bridge. Tries @p lastKnownHost first (dotted-quad or
 * hostname; empty / unresolvable is skipped), then sweeps the local /24
 * on @p port, reporting progress through @p onProgress (may be nullptr).
 *
 * @param outHost receives the bridge host on success: @p lastKnownHost
 *                verbatim if it answered, otherwise the dotted-quad
 *                found by the sweep. Untouched on failure.
 * @return true if a bridge was found.
 */
bool find(const char* lastKnownHost, uint16_t port, String& outHost,
          ProgressHandler onProgress);

}  // namespace BridgeDiscovery
