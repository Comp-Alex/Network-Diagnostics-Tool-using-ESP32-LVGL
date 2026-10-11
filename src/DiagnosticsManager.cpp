#include "DiagnosticsManager.h"

#include <WiFi.h>
#include <apps/ping/ping_sock.h>

namespace {
constexpr uint8_t PingCount = 3;

struct PingContext {
  esp_ping_handle_t handle = nullptr;
  volatile uint8_t replies = 0;
  volatile uint32_t latencyTotal = 0;
  volatile bool complete = false;
  String target;
};

PingContext gatewayPing;
PingContext dnsPing;

void onPingReply(esp_ping_handle_t handle, void *args) {
  auto *context = static_cast<PingContext *>(args);
  uint32_t elapsed = 0;
  if (esp_ping_get_profile(handle, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed)) == ESP_OK) {
    context->latencyTotal += elapsed;
  }
  if (context->replies < PingCount) context->replies++;
}

void onPingEnd(esp_ping_handle_t, void *args) {
  static_cast<PingContext *>(args)->complete = true;
}

bool startProbe(PingContext &context, const IPAddress &address) {
  if (context.handle) {
    esp_ping_stop(context.handle);
    esp_ping_delete_session(context.handle);
    context.handle = nullptr;
  }
  context.replies = 0;
  context.latencyTotal = 0;
  context.complete = false;
  context.target = address.toString();

  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.count = PingCount;
  config.interval_ms = 400;
  config.timeout_ms = 700;
  config.target_addr.type = IPADDR_TYPE_V4;
  IP_ADDR4(&config.target_addr, address[0], address[1], address[2], address[3]);

  esp_ping_callbacks_t callbacks {};
  callbacks.cb_args = &context;
  callbacks.on_ping_success = onPingReply;
  callbacks.on_ping_timeout = nullptr;
  callbacks.on_ping_end = onPingEnd;
  if (esp_ping_new_session(&config, &callbacks, &context.handle) != ESP_OK) return false;
  if (esp_ping_start(context.handle) != ESP_OK) {
    esp_ping_delete_session(context.handle);
    context.handle = nullptr;
    return false;
  }
  return true;
}

DiagnosticsManager::ProbeResult snapshot(PingContext &context) {
  DiagnosticsManager::ProbeResult result {};
  result.complete = context.complete;
  result.replies = context.replies;
  result.attempts = PingCount;
  result.averageMs = context.replies ? context.latencyTotal / context.replies : 0;
  result.target = context.target;
  if (result.complete && context.handle) {
    esp_ping_delete_session(context.handle);
    context.handle = nullptr;
  }
  return result;
}
}  // namespace

namespace DiagnosticsManager {
bool start() {
  if (WiFi.status() != WL_CONNECTED) return false;
  IPAddress gateway = WiFi.gatewayIP();
  IPAddress dns = WiFi.dnsIP(0);
  if (dns == IPAddress(0, 0, 0, 0)) dns = IPAddress(8, 8, 8, 8);
  bool gatewayStarted = startProbe(gatewayPing, gateway);
  bool dnsStarted = startProbe(dnsPing, dns);
  return gatewayStarted && dnsStarted;
}

ProbeResult gatewayResult() {
  return snapshot(gatewayPing);
}

ProbeResult dnsResult() {
  return snapshot(dnsPing);
}
}  // namespace DiagnosticsManager
