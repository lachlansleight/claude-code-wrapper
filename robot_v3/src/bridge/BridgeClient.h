#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Bridge {

using MessageHandler = void (*)(JsonDocument& doc);
using ConnectionHandler = void (*)(bool connected);

void begin(const char* host, uint16_t port, const char* token);
void tick();
bool isConnected();

void onMessage(MessageHandler handler);
void onConnection(ConnectionHandler handler);

bool sendRaw(const char* json);
bool requestSessions();

}  // namespace Bridge
