#pragma once

#include <Arduino.h>

namespace StorageManager {
bool begin();
bool available();
void logWidsAlert(const String &timestamp, const String &event);
const char *statusText();
}  // namespace StorageManager
