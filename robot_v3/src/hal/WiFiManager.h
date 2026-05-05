#pragma once

#include <Arduino.h>

/**
 * @file WiFiManager.h
 * @brief Thin wrapper over `WiFi.h`: connect-with-timeout + auto-reconnect.
 *
 * No SSID list, no scanning — that lives in Provisioning, which iterates
 * known networks at boot and calls tryConnect() on each in turn. Once
 * the firmware is running, tick() keeps the chosen network alive: every
 * 2 s it checks WiFi.status() and forces a `WiFi.begin()` if the link
 * has dropped.
 */
namespace WifiMgr {

/**
 * Synchronously join @p ssid / @p password in STA mode. Polls every
 * 250 ms (printing a dot to Serial as it goes), and gives up at
 * @p timeoutMs. Returns true on success, false on timeout (logs warn
 * either way and disconnects on failure).
 */
bool tryConnect(const char* ssid, const char* password, uint32_t timeoutMs);

/**
 * Auto-reconnect tick. If the link is down, kicks `WiFi.begin(ssid,
 * password)` again. Throttled to once every 2 s. Call every loop.
 */
void tick(const char* ssid, const char* password);

/// True if `WiFi.status() == WL_CONNECTED`.
bool isConnected();

/// Current STA IPv4 address as a printable string ("0.0.0.0" if down).
String ip();

}  // namespace WifiMgr
