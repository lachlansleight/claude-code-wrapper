#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * @file BridgeClient.h
 * @brief WebSocket client to the agent bridge: connect, receive JSON, dispatch.
 *
 * Owns a single `WebSocketsClient` configured with auto-reconnect and a
 * heartbeat (15 s interval, 3 s ping timeout, 2 consecutive failures =
 * dead). Inbound text frames are JSON-parsed into an ArduinoJson
 * `JsonDocument` and forwarded to the registered MessageHandler. The
 * higher-level dispatcher (typically `AgentEvents::dispatch` and
 * `BridgeControl::dispatch`) decides what each `type` means.
 *
 * The bridge URL is `ws://<host>:<port>/ws?token=<token>`. Host, port
 * and token come from Provisioning at boot.
 */
namespace Bridge {

/// Called for every successfully parsed inbound frame.
using MessageHandler = void (*)(JsonDocument& doc);

/// Called on each connect / disconnect transition.
using ConnectionHandler = void (*)(bool connected);

/**
 * Open the WebSocket and start auto-reconnect + heartbeat. Connection
 * is asynchronous — isConnected() returns true after the
 * `WStype_CONNECTED` event fires. Subsequent calls reconfigure.
 */
void begin(const char* host, uint16_t port, const char* token);

/// Pump the WebSocket state machine (`ws.loop`). Call every loop.
void tick();

/// True between `WStype_CONNECTED` and `WStype_DISCONNECTED`.
bool isConnected();

/**
 * Register a handler called for every parsed inbound text frame. There
 * is exactly one slot — later registrations replace earlier ones.
 * Parse failures log a warning and are silently dropped.
 */
void onMessage(MessageHandler handler);

/// Register a connection state change handler. Single slot, replaces previous.
void onConnection(ConnectionHandler handler);

/**
 * Send an arbitrary JSON string. Returns true if the frame was queued
 * for transmission, false if the socket is not connected. No
 * validation is performed on the payload.
 */
bool sendRaw(const char* json);

/**
 * Convenience: send `{"type":"request_sessions"}` to ask the bridge to
 * publish the current `active_sessions` list. Used to recover the
 * latched session on reconnect.
 */
bool requestSessions();

}  // namespace Bridge
