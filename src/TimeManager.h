#pragma once

#include <Arduino.h>

namespace TimeManager {
void begin();
void service(bool wifiConnected);
bool timeIsValid();
bool rtcIsPresent();
bool ntpIsSynchronized();
String dateString();
String timeString();
String timestamp();
}  // namespace TimeManager
