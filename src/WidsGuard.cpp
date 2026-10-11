#include "WidsGuard.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include "Config.h"
#include "RadioManager.h"

namespace {
constexpr uint8_t MaxTrackedSsids = 12;
constexpr uint8_t AlertQueueSize = 4;
constexpr uint32_t ChannelDwellMs = 350;
constexpr uint32_t DeauthAlertCooldownMs = 5000;

struct SsidObservation {
  char ssid[33];
  uint8_t firstBssid[6];
  bool firstPrivacy;
  bool used;
  bool reported;
};

SsidObservation ssidObservations[MaxTrackedSsids] {};
WidsGuard::Alert alertQueue[AlertQueueSize] {};
portMUX_TYPE alertMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t alertHead = 0;
volatile uint8_t alertTail = 0;
volatile uint8_t alertCount = 0;
volatile uint8_t deauthCount = 0;
volatile uint32_t deauthWindowStarted = 0;
volatile uint32_t lastDeauthAlert = 0;
uint8_t channel = 1;
uint32_t lastChannelChange = 0;

void queueAlert(const char *message) {
  portENTER_CRITICAL(&alertMux);
  if (alertCount < AlertQueueSize) {
    strlcpy(alertQueue[alertTail].message, message, sizeof(alertQueue[alertTail].message));
    alertTail = (alertTail + 1) % AlertQueueSize;
    alertCount++;
  }
  portEXIT_CRITICAL(&alertMux);
}

void formatMac(const uint8_t *mac, char *output, size_t size) {
  snprintf(output, size, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void checkBeacon(const uint8_t *frame, uint16_t length, uint8_t subtype) {
  if (length < 36) return;
  uint8_t bssid[6];
  memcpy(bssid, frame + 10, sizeof(bssid));
  bool privacy = (frame[35] & 0x10) != 0;
  uint16_t position = 36;
  while (position + 2 <= length) {
    uint8_t elementId = frame[position];
    uint8_t elementLength = frame[position + 1];
    position += 2;
    if (position + elementLength > length) return;
    if (elementId == 0 && elementLength > 0 && elementLength <= 32) {
      char ssid[33] {};
      memcpy(ssid, frame + position, elementLength);
      for (uint8_t index = 0; index < MaxTrackedSsids; index++) {
        SsidObservation &seen = ssidObservations[index];
        if (!seen.used) {
          strlcpy(seen.ssid, ssid, sizeof(seen.ssid));
          memcpy(seen.firstBssid, bssid, sizeof(bssid));
          seen.firstPrivacy = privacy;
          seen.used = true;
          return;
        }
        if (strncmp(seen.ssid, ssid, sizeof(seen.ssid)) != 0) continue;
        bool isDifferentBssid = memcmp(seen.firstBssid, bssid, sizeof(bssid)) != 0;
        if (isDifferentBssid && seen.firstPrivacy != privacy && !seen.reported) {
          char mac[18];
          char message[80];
          formatMac(bssid, mac, sizeof(mac));
          snprintf(message, sizeof(message), "Possible evil twin: %.28s security differs at %.17s", ssid, mac);
          queueAlert(message);
          seen.reported = true;
        }
        return;
      }
      return;
    }
    position += elementLength;
  }
  (void)subtype;
}

void receiveFrame(void *buffer, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT || buffer == nullptr) return;
  auto *packet = static_cast<wifi_promiscuous_pkt_t *>(buffer);
  const uint8_t *frame = packet->payload;
  const uint16_t length = packet->rx_ctrl.sig_len;
  if (length < 24) return;
  uint16_t frameControl = static_cast<uint16_t>(frame[0] | (frame[1] << 8));
  uint8_t subtype = (frameControl >> 4) & 0x0F;

  if (subtype == 12 || subtype == 10) {
    uint32_t now = millis();
    portENTER_CRITICAL(&alertMux);
    if (now - deauthWindowStarted > NdtConfig::WidsDeauthWindowMs) {
      deauthWindowStarted = now;
      deauthCount = 0;
    }
    if (deauthCount < UINT8_MAX) deauthCount++;
    const bool alert = deauthCount >= NdtConfig::WidsDeauthThreshold &&
                       now - lastDeauthAlert >= DeauthAlertCooldownMs;
    if (alert) lastDeauthAlert = now;
    portEXIT_CRITICAL(&alertMux);

    if (alert) {
      char mac[18];
      char message[80];
      formatMac(frame + 10, mac, sizeof(mac));
      snprintf(message, sizeof(message), "Deauth/disassoc burst from %.17s", mac);
      queueAlert(message);
    }
  } else if (subtype == 8 || subtype == 5) {
    checkBeacon(frame, length, subtype);
  }
}
}  // namespace

namespace WidsGuard {
void begin() {
  pinMode(NdtConfig::AlertSpeakerPin, OUTPUT);
  digitalWrite(NdtConfig::AlertSpeakerPin, LOW);
}

bool start() {
  memset(ssidObservations, 0, sizeof(ssidObservations));
  portENTER_CRITICAL(&alertMux);
  alertHead = alertTail = alertCount = 0;
  deauthCount = 0;
  deauthWindowStarted = millis();
  portEXIT_CRITICAL(&alertMux);
  channel = 1;
  lastChannelChange = millis();
  if (!RadioManager::switchMode(MODE_WIFI_PROMISCUOUS, receiveFrame)) return false;
  return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

void stop() {
  RadioManager::switchMode(MODE_WIFI_STA);
}

void service() {
  if (RadioManager::currentMode() != MODE_WIFI_PROMISCUOUS) return;
  const uint32_t now = millis();
  if (now - lastChannelChange < ChannelDwellMs) return;
  lastChannelChange = now;
  channel = channel >= 13 ? 1 : channel + 1;
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

bool popAlert(Alert &alert) {
  portENTER_CRITICAL(&alertMux);
  if (alertCount == 0) {
    portEXIT_CRITICAL(&alertMux);
    return false;
  }
  alert = alertQueue[alertHead];
  alertHead = (alertHead + 1) % AlertQueueSize;
  alertCount--;
  portEXIT_CRITICAL(&alertMux);
  tone(NdtConfig::AlertSpeakerPin, 2200, 120);
  return true;
}

uint8_t currentChannel() {
  return channel;
}
}  // namespace WidsGuard
