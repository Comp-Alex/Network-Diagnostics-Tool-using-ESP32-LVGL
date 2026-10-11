#include "TrafficMonitor.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include "RadioManager.h"

namespace {
constexpr uint32_t ChannelDwellMs = 350;

portMUX_TYPE countersMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t frameCount = 0;
volatile uint32_t managementCount = 0;
volatile uint32_t controlCount = 0;
volatile uint32_t dataCount = 0;
volatile uint32_t otherCount = 0;
volatile uint64_t byteCount = 0;
volatile int64_t rssiTotal = 0;
uint8_t currentChannel = 1;
uint32_t lastChannelChange = 0;
bool monitorRunning = false;

void receiveFrame(void *buffer, wifi_promiscuous_pkt_type_t type) {
  if (buffer == nullptr) return;
  const auto *packet = static_cast<const wifi_promiscuous_pkt_t *>(buffer);
  const uint16_t length = packet->rx_ctrl.sig_len;
  if (length < 2) return;

  portENTER_CRITICAL(&countersMux);
  frameCount++;
  byteCount += length;
  rssiTotal += packet->rx_ctrl.rssi;
  switch (type) {
    case WIFI_PKT_MGMT: managementCount++; break;
    case WIFI_PKT_CTRL: controlCount++; break;
    case WIFI_PKT_DATA: dataCount++; break;
    default: otherCount++; break;
  }
  portEXIT_CRITICAL(&countersMux);
}
}  // namespace

namespace TrafficMonitor {
bool start() {
  portENTER_CRITICAL(&countersMux);
  frameCount = managementCount = controlCount = dataCount = otherCount = 0;
  byteCount = 0;
  rssiTotal = 0;
  portEXIT_CRITICAL(&countersMux);

  currentChannel = 1;
  lastChannelChange = millis();
  if (!RadioManager::switchMode(MODE_WIFI_PROMISCUOUS, receiveFrame,
                                WIFI_PROMIS_FILTER_MASK_ALL)) {
    monitorRunning = false;
    return false;
  }
  if (esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    RadioManager::switchMode(MODE_WIFI_STA);
    monitorRunning = false;
    return false;
  }
  monitorRunning = true;
  return true;
}

void stop() {
  if (!monitorRunning) return;
  esp_wifi_set_promiscuous(false);
  RadioManager::switchMode(MODE_WIFI_STA);
  monitorRunning = false;
}

void service() {
  if (!monitorRunning || RadioManager::currentMode() != MODE_WIFI_PROMISCUOUS) return;
  const uint32_t now = millis();
  if (now - lastChannelChange < ChannelDwellMs) return;
  lastChannelChange = now;
  currentChannel = currentChannel >= 13 ? 1 : currentChannel + 1;
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}

bool running() {
  return monitorRunning && RadioManager::currentMode() == MODE_WIFI_PROMISCUOUS;
}

Snapshot snapshot() {
  Snapshot result {};
  portENTER_CRITICAL(&countersMux);
  result.frames = frameCount;
  result.managementFrames = managementCount;
  result.controlFrames = controlCount;
  result.dataFrames = dataCount;
  result.otherFrames = otherCount;
  result.observedBytes = byteCount;
  result.averageRssi = frameCount ? static_cast<int32_t>(rssiTotal / frameCount) : 0;
  result.channel = currentChannel;
  portEXIT_CRITICAL(&countersMux);
  return result;
}
}  // namespace TrafficMonitor
