#pragma once

#include <Arduino.h>

namespace DiagnosticsManager {
struct ProbeResult {
  bool complete;
  uint8_t replies;
  uint8_t attempts;
  uint32_t averageMs;
  String target;
};

bool start();
ProbeResult gatewayResult();
ProbeResult dnsResult();
}  // namespace DiagnosticsManager
