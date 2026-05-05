#pragma once

#include <Arduino.h>

/**
 * @file Provisioning.h
 * @brief NVS-backed credentials store + captive-AP setup portal.
 *
 * Stores up to `kMaxKnownNetworks` known WiFi networks, each with its
 * own bridge endpoint (host/port/token). On boot the main sketch reads
 * them in order and tries each via `WifiMgr::tryConnect`; the first to
 * succeed wins and is also "remembered" (promoted to head of the list).
 *
 * Networks are packed in NVS as a single tab/newline-delimited string
 * under the `nets` key inside the `bridge_cfg` namespace. The legacy
 * `ssid/pass/host/port/token` keys still hold the *most recently used*
 * single set, used both as defaults for the portal form and as a
 * fallback for entries without their own bridge fields.
 *
 * The portal is a blocking, soft-AP captive HTTP form on
 * `robot-XXXX` (MAC-derived SSID). It is entered if the user holds
 * `PORTAL_BUTTON_PIN` (GPIO 0 by default) for 800 ms at boot, or if a
 * one-shot request was set via requestOneTimePortalRequest(). Saving
 * triggers a reboot.
 *
 * Optional UI integration is delivered via onPortalState — register a
 * callback that paints the SSID/IP somewhere visible (see
 * ProvisioningUI for the display glue).
 */
namespace Provisioning {

/**
 * Single resolved configuration set: the credentials currently being
 * used for WiFi + the bridge. Returned by load() and consumed by
 * runPortal() as the form's initial values.
 */
struct Config {
  String wifi_ssid;       ///< WiFi SSID.
  String wifi_password;   ///< WPA passphrase, or empty for open networks.
  String bridge_host;     ///< Bridge IP / hostname.
  uint16_t bridge_port;   ///< Bridge TCP port (default `BRIDGE_PORT`).
  String bridge_token;    ///< Shared secret accepted by the bridge.
};

/**
 * One entry in the multi-network list. Unlike Config, this represents
 * a *known* network that we've successfully used before. Each entry
 * carries its own bridge endpoint so different LANs can target
 * different bridge hosts.
 */
struct NetEntry {
  String ssid;
  String password;
  String bridge_host;
  uint16_t bridge_port;
  String bridge_token;
};

/// Callback signature for portal lifecycle notifications. @see onPortalState.
using PortalStateHandler = void (*)(const char* ssid, const char* ip);

/// Maximum number of remembered networks (FIFO with most-recent-first).
static constexpr size_t kMaxKnownNetworks = 8;

/**
 * Read the legacy single-set credentials into @p out, falling back to
 * the compiled-in `WIFI_SSID`/`BRIDGE_*` macros from `config.h` for any
 * missing key. Returns true if **all** keys were present (so callers
 * can distinguish "fresh device" from "previously provisioned"). Reads
 * are non-mutating.
 */
bool load(Config& out);

/// Persist the legacy single-set credentials. Used by the portal save handler.
void save(const Config& c);

/**
 * Read up to @p maxCount remembered networks into @p out, in stored
 * (most-recent-first) order. Returns the count actually read. Entries
 * with empty bridge fields inherit from the legacy defaults.
 */
size_t loadNetworks(NetEntry* out, size_t maxCount);

/**
 * Insert/promote @p entry to the head of the remembered-networks list.
 * If an entry with the same SSID already exists it is removed first
 * (de-duplication). The list is capped at `kMaxKnownNetworks - 1`
 * entries before the new one is prepended. Also updates the legacy
 * single-set keys so a fresh boot finds the right defaults.
 */
void rememberNetwork(const NetEntry& entry);

/// Wipe the entire `bridge_cfg` NVS namespace (all networks + legacy keys).
void clear();

/**
 * Set a one-shot flag in NVS that causes the next boot to enter the
 * portal regardless of the BOOT-button hold. Used by the
 * `provision-once` serial command.
 */
void requestOneTimePortal();

/**
 * Read-and-clear the one-shot portal flag set by
 * requestOneTimePortal(). Returns true if a one-shot was pending.
 */
bool consumeOneTimePortalRequest();

/**
 * Sample `PORTAL_BUTTON_PIN` (GPIO 0 by default, INPUT_PULLUP) for
 * 800 ms; returns true only if the line stayed LOW the entire time.
 * Blocks for the full window — call early in `setup()`.
 */
bool shouldEnterPortal();

/**
 * Register a callback invoked once the soft-AP is up, with the AP SSID
 * and IP as C-strings. Used by ProvisioningUI to draw the portal
 * splash on the OLED while the portal is blocking. Pass nullptr to
 * clear.
 */
void onPortalState(PortalStateHandler handler);

/**
 * Enter the captive provisioning portal. Switches the radio to AP-only,
 * starts a soft-AP on `robot-XXXX`, fires the registered
 * PortalStateHandler, then runs an HTTP server on port 80 with a single
 * `/` form (initial values from @p currentCfg), `/save` and `/forget`
 * endpoints, and `/forget_all`. Saving and Forgetting both reboot via
 * `ESP.restart()`. **Blocks forever** — the only exit is reboot.
 */
void runPortal(const Config& currentCfg);

}  // namespace Provisioning
