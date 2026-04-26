#pragma once

// WebSocket client to the Claude Code bridge at ws://<host>:<port>/ws
//
// Handles auth, auto-reconnect, JSON parsing, and forwards every decoded
// message to AgentEvents::dispatch(). Sending is also exposed here so the
// firmware can approve/deny permissions or inject chat messages.

#include <Arduino.h>

namespace Bridge {

void begin(const char* host, uint16_t port, const char* token);

// Call from loop(). Pumps the underlying WebSocketsClient state machine.
void tick();

bool isConnected();

// ---- Send helpers ----------------------------------------------------------

// Reply to / approve a tool-use permission. request_id is the 5-letter
// identifier from a PermissionRequestEvent. behavior is "allow" or "deny".
bool sendPermissionVerdict(const char* request_id, const char* behavior);

// Inject a user message into Claude. chat_id can be empty to let the
// bridge generate one. Returns false if not connected.
bool sendChatMessage(const char* content, const char* chat_id = "");

// Low-level: send a raw JSON string. Caller is responsible for formatting.
bool sendRaw(const char* json);

}  // namespace Bridge
