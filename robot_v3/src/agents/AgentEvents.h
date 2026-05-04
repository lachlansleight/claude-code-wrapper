#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace AgentEvents {

struct Event {
  const char* kind;
  const char* agent;
  const char* sessionId;
  const char* turnId;
  JsonVariantConst event;
  JsonVariantConst envelope;
};

struct AgentState {
  bool wsConnected;
  char latchedSession[40];
  char lastEventKind[40];
  char pendingPermission[64];
};

using EventHandler = void (*)(const Event&);
using ConnectionHandler = void (*)(bool connected);

void begin();
const AgentState& state();

void onEvent(EventHandler handler);
void onConnectionChange(ConnectionHandler handler);

void notifyConnection(bool connected);
void dispatch(JsonDocument& doc);

}  // namespace AgentEvents
