#pragma once

#include <Arduino.h>
#include <esp_wifi.h>

enum SystemMode : uint8_t {
  MODE_WIFI_STA,
  MODE_WIFI_PROMISCUOUS,
  MODE_BLE_SCAN
};

namespace RadioManager {
bool begin();
bool switchMode(SystemMode mode, wifi_promiscuous_cb_t callback = nullptr,
                uint32_t promiscuousFilterMask = WIFI_PROMIS_FILTER_MASK_MGMT);
SystemMode currentMode();
const char *modeName();
void rememberWifiCredentials(const String &ssid, const String &password);
String rememberedSsid();
bool wifiIsConnected();
}  // namespace RadioManager
