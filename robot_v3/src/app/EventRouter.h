#pragma once

#include <ArduinoJson.h>

namespace EventRouter {

void begin();
void tick();

void onBridgeMessage(JsonDocument& doc);
void onBridgeConnection(bool connected);

}  // namespace EventRouter
