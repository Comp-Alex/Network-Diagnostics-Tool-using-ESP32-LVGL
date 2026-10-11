#include "RadioManager.h"

#include <BLEDevice.h>
#include <BLEScan.h>
#include <WiFi.h>

namespace {
SystemMode activeMode = MODE_WIFI_STA;
bool initialized = false;
String wifiSsid;
String wifiPassword;

bool isBleMode(SystemMode mode) {
  return mode == MODE_BLE_SCAN;
}

void stopBleStack() {
  if (!BLEDevice::getInitialized()) return;
  BLEScan *scanner = BLEDevice::getScan();
  if (scanner) scanner->stop();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  if (advertising) advertising->stop();
  BLEDevice::deinit(false);
}

void connectRememberedWifi() {
  if (wifiSsid.length() > 0) WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
}
}  // namespace

namespace RadioManager {
bool begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  activeMode = MODE_WIFI_STA;
  initialized = true;
  return true;
}

bool switchMode(SystemMode mode, wifi_promiscuous_cb_t callback,
                uint32_t promiscuousFilterMask) {
  if (!initialized) begin();
  if (mode == activeMode && mode != MODE_WIFI_PROMISCUOUS) return true;

  if (activeMode == MODE_WIFI_PROMISCUOUS) {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
  }
  if (isBleMode(activeMode)) stopBleStack();

  if (isBleMode(mode)) {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    delay(40);
    BLEDevice::init("");
    activeMode = mode;
    return true;
  }

  if (mode == MODE_WIFI_PROMISCUOUS) {
    WiFi.mode(WIFI_OFF);
    delay(40);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    if (callback == nullptr) return false;
    wifi_promiscuous_filter_t filter {};
    filter.filter_mask = promiscuousFilterMask;
    if (esp_wifi_set_promiscuous_filter(&filter) != ESP_OK) return false;
    if (esp_wifi_set_promiscuous_rx_cb(callback) != ESP_OK) return false;
    if (esp_wifi_set_promiscuous(true) != ESP_OK) return false;
    activeMode = mode;
    return true;
  }

  if (activeMode == MODE_WIFI_PROMISCUOUS || WiFi.getMode() == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_OFF);
    delay(40);
  }
  if (!WiFi.mode(WIFI_STA)) return false;
  activeMode = mode;

  if (mode == MODE_WIFI_STA) {
    connectRememberedWifi();
  }
  return true;
}

SystemMode currentMode() {
  return activeMode;
}

const char *modeName() {
  switch (activeMode) {
    case MODE_WIFI_STA: return "STA";
    case MODE_WIFI_PROMISCUOUS: return "MON";
    case MODE_BLE_SCAN: return "BLE";
    default: return "OFF";
  }
}

void rememberWifiCredentials(const String &ssid, const String &password) {
  wifiSsid = ssid;
  wifiPassword = password;
}

String rememberedSsid() {
  return wifiSsid;
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}
}  // namespace RadioManager
