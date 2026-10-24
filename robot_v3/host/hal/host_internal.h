#pragma once
// Glue between the host HAL files and host/core/sultana_core.cpp.

#include <ArduinoJson.h>
#include <stdint.h>

namespace SultanaHost {

// Display_host.cpp
const uint16_t* pushedFrame();  // last frame passed to Display::pushFrame()
uint32_t pushCount();

// Bridge_host.cpp
void setConnected(bool connected);
void deliver(JsonDocument& doc);

// sultana_core.cpp
bool send(const char* json);

}  // namespace SultanaHost
