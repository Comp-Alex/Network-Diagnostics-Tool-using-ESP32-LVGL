#pragma once

#include <Arduino.h>

namespace SubnetScanner {
bool start();
void service();
bool running();
bool complete();
uint8_t progressPercent();
String discoveredHosts();
}  // namespace SubnetScanner
