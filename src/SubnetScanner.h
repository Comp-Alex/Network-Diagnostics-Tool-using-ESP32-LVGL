#pragma once

#include <Arduino.h>

namespace SubnetScanner {
bool start();
bool startDeviceDiscovery();
void cancel();
void service();
bool running();
bool complete();
bool deviceDiscovery();
uint8_t progressPercent();
String discoveredHosts();
}  // namespace SubnetScanner
