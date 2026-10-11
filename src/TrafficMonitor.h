#pragma once

#include <Arduino.h>

namespace TrafficMonitor {
struct Snapshot {
  uint32_t frames;
  uint32_t managementFrames;
  uint32_t controlFrames;
  uint32_t dataFrames;
  uint32_t otherFrames;
  uint64_t observedBytes;
  int32_t averageRssi;
  uint8_t channel;
};

bool start();
void stop();
void service();
bool running();
Snapshot snapshot();
}  // namespace TrafficMonitor
