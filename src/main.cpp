#include <Arduino.h>
#include <BLEAddress.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <lvgl.h>

#include "Config.h"
#include "DiagnosticsManager.h"
#include "RadioManager.h"
#include "StorageManager.h"
#include "SubnetScanner.h"
#include "TimeManager.h"
#include "TrafficMonitor.h"
#include "WidsGuard.h"

namespace {
constexpr uint16_t ScreenWidth = NdtConfig::ScreenWidth;
constexpr uint16_t ScreenHeight = NdtConfig::ScreenHeight;
constexpr uint8_t TftBacklightPin = NdtConfig::TftBacklightPin;
constexpr uint8_t TouchSckPin = NdtConfig::TouchSckPin;
constexpr uint8_t TouchMisoPin = NdtConfig::TouchMisoPin;
constexpr uint8_t TouchMosiPin = NdtConfig::TouchMosiPin;
constexpr uint8_t TouchCsPin = NdtConfig::TouchCsPin;
constexpr uint8_t TouchIrqPin = NdtConfig::TouchIrqPin;
constexpr uint8_t MaxNetworks = NdtConfig::MaxNetworks;
constexpr uint8_t MaxBleDevices = NdtConfig::MaxBleDevices;
constexpr uint8_t MaxLogEntries = NdtConfig::MaxLogEntries;
constexpr uint32_t BleScanSeconds = NdtConfig::BleScanSeconds;
constexpr uint32_t WiFiConnectionTimeoutMs = NdtConfig::WiFiConnectionTimeoutMs;

TFT_eSPI display;
XPT2046_Touchscreen touch(TouchCsPin, TouchIrqPin);
Preferences preferences;

lv_disp_draw_buf_t drawBuffer;
lv_color_t buffer[ScreenWidth * 16];
lv_obj_t *screen = nullptr;
lv_obj_t *clockLabel = nullptr;
lv_obj_t *radioStatusLabel = nullptr;
lv_obj_t *sdStatusLabel = nullptr;
lv_obj_t *ipStatusLabel = nullptr;
lv_obj_t *pageBody = nullptr;
lv_obj_t *pageStatus = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *networkList = nullptr;
lv_obj_t *bleDeviceList = nullptr;
lv_obj_t *passwordDialog = nullptr;
lv_obj_t *darkModeSwitch = nullptr;
lv_obj_t *splashScreen = nullptr;
lv_obj_t *winkLid = nullptr;
lv_obj_t *signalBar = nullptr;
lv_obj_t *channelChart = nullptr;
lv_obj_t *latencyLabel = nullptr;
lv_obj_t *lossLabel = nullptr;
lv_obj_t *logList = nullptr;
lv_chart_series_t *channelSeries = nullptr;
lv_obj_t *widsStatusLabel = nullptr;
lv_obj_t *subnetResultsLabel = nullptr;
lv_obj_t *gatewayPingLabel = nullptr;
lv_obj_t *dnsPingLabel = nullptr;
lv_obj_t *widsSwitch = nullptr;
lv_obj_t *trafficStatusLabel = nullptr;
lv_obj_t *trafficToggleSwitch = nullptr;
lv_obj_t *trafficDetailsLabel = nullptr;
lv_obj_t *connectionStatusLabel = nullptr;
lv_obj_t *bleDisconnectButton = nullptr;
lv_obj_t *wifiModeButton = nullptr;
lv_obj_t *bleModeButton = nullptr;
lv_obj_t *connectivityScanButton = nullptr;
lv_obj_t *connectivityScanButtonLabel = nullptr;
BLEClient *bleClient = nullptr;

void createDashboard();
void createDiagnosticsPage();
void createTrafficMonitorPage();
void createWidsPage();
void createConnectivityPage();
void createDashboardBaseForSubpage(const char *headingText);

struct NetworkInfo {
  String ssid;
  int32_t rssi;
  int32_t channel;
  uint8_t encryption;
};

struct BleDeviceInfo {
  String name;
  String address;
  int32_t rssi;
  bool possibleTracker;
  esp_ble_addr_type_t addressType;
};

NetworkInfo networks[MaxNetworks];
BleDeviceInfo bleDevices[MaxBleDevices];
uint8_t networkCount = 0;
uint8_t bleDeviceCount = 0;
String logs[MaxLogEntries];
uint8_t logCount = 0;
uint32_t wifiAttemptStarted = 0;
bool wifiConnecting = false;
bool widsActive = false;
bool trafficMonitorActive = false;
bool darkMode = true;
String pendingSsid;
String connectedBleName;
uint32_t lastStatusRefresh = 0;
bool diagnosticsRunning = false;
volatile bool bleScanDone = false;
enum class ScanJob : uint8_t { Idle, Wifi, Ble };
ScanJob scanJob = ScanJob::Idle;
enum class ConnectivityView : uint8_t { Wifi, Bluetooth };
ConnectivityView connectivityView = ConnectivityView::Wifi;

lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_event_cb_t callback);
void openPasswordEntry(const String &ssid);

lv_color_t themeBackgroundColor() {
  return lv_color_hex(darkMode ? 0x000000 : 0xEEF4EF);
}

lv_color_t themeSurfaceColor() {
  return lv_color_hex(darkMode ? 0x07110A : 0xFFFFFF);
}

lv_color_t themeAccentColor() {
  return lv_color_hex(darkMode ? 0x39FF14 : 0x168A3A);
}

lv_color_t themeTextColor() {
  return lv_color_hex(darkMode ? 0xE8F5E9 : 0x17251A);
}

lv_color_t themeBorderColor() {
  return lv_color_hex(darkMode ? 0x1B5E20 : 0xA8C9AD);
}

void applyThemeToObjectTree(lv_obj_t *object) {
  lv_obj_set_style_text_color(object, themeTextColor(), LV_PART_MAIN);
  if (lv_obj_check_type(object, &lv_label_class)) {
    lv_obj_set_style_text_color(object, themeTextColor(), LV_PART_MAIN);
  } else {
    lv_obj_set_style_bg_color(object, themeSurfaceColor(), LV_PART_MAIN);
    lv_obj_set_style_border_color(object, themeBorderColor(), LV_PART_MAIN);
    if (lv_obj_check_type(object, &lv_btn_class)) {
      lv_obj_set_style_bg_color(object, themeAccentColor(), LV_PART_MAIN | LV_STATE_CHECKED);
      lv_obj_set_style_text_color(object, themeBackgroundColor(), LV_PART_MAIN | LV_STATE_CHECKED);
    }
    if (lv_obj_check_type(object, &lv_switch_class)) {
      lv_obj_set_style_bg_color(object, themeBorderColor(), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(object, themeAccentColor(), LV_PART_INDICATOR | LV_STATE_CHECKED);
      lv_obj_set_style_bg_color(object, themeTextColor(), LV_PART_KNOB);
    }
  }

  for (uint32_t index = 0; index < lv_obj_get_child_cnt(object); index++) {
    applyThemeToObjectTree(lv_obj_get_child(object, index));
  }
}

void applyDashboardTheme() {
  if (!screen) return;
  applyThemeToObjectTree(screen);
  lv_obj_set_style_bg_color(screen, themeBackgroundColor(), LV_PART_MAIN);
  if (titleLabel) lv_obj_set_style_text_color(titleLabel, themeAccentColor(), LV_PART_MAIN);
  if (statusLabel) lv_obj_set_style_text_color(statusLabel, themeTextColor(), LV_PART_MAIN);
  if (latencyLabel) lv_obj_set_style_text_color(latencyLabel, themeAccentColor(), LV_PART_MAIN);
  if (lossLabel) lv_obj_set_style_text_color(lossLabel, themeAccentColor(), LV_PART_MAIN);
  if (channelChart && channelSeries) lv_chart_set_series_color(channelChart, channelSeries, themeAccentColor());
  lv_obj_invalidate(screen);
}

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colorBuffer) {
  const uint32_t width = area->x2 - area->x1 + 1;
  const uint32_t height = area->y2 - area->y1 + 1;
  display.startWrite();
  display.setAddrWindow(area->x1, area->y1, width, height);
  display.pushColors(reinterpret_cast<uint16_t *>(&colorBuffer->full), width * height, true);
  display.endWrite();
  lv_disp_flush_ready(driver);
}

void readTouch(lv_indev_drv_t *driver, lv_indev_data_t *data) {
  if (!touch.touched()) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  TS_Point point = touch.getPoint();
  data->state = LV_INDEV_STATE_PR;
  data->point.x = map(point.x, 250, 3850, 0, ScreenWidth);
  data->point.y = map(point.y, 200, 3850, 0, ScreenHeight);
  data->point.x = constrain(data->point.x, 0, ScreenWidth - 1);
  data->point.y = constrain(data->point.y, 0, ScreenHeight - 1);
}

void addLog(const String &message) {
  String entry = TimeManager::timestamp() + "  " + message;
  if (logCount < MaxLogEntries) {
    logs[logCount++] = entry;
  } else {
    for (uint8_t index = 1; index < MaxLogEntries; index++) logs[index - 1] = logs[index];
    logs[MaxLogEntries - 1] = entry;
  }
  preferences.putUChar("log_count", logCount);
  for (uint8_t index = 0; index < logCount; index++) preferences.putString(("log" + String(index)).c_str(), logs[index]);
}

void loadLogs() {
  logCount = min(preferences.getUChar("log_count", 0), MaxLogEntries);
  for (uint8_t index = 0; index < logCount; index++) logs[index] = preferences.getString(("log" + String(index)).c_str(), "");
}

void setStatus(const String &message) {
  if (pageStatus) lv_label_set_text(pageStatus, message.c_str());
  else if (connectionStatusLabel) lv_label_set_text(connectionStatusLabel, message.c_str());
  else if (statusLabel) lv_label_set_text(statusLabel, message.c_str());
}

void clearContainer(lv_obj_t *container) {
  lv_obj_clean(container);
}

void showLogs(lv_event_t *) {
  if (!logList) return;
  clearContainer(logList);
  if (logCount == 0) {
    lv_obj_t *empty = lv_label_create(logList);
    lv_label_set_text(empty, "No saved diagnostics yet");
    return;
  }
  for (int index = logCount - 1; index >= 0; index--) {
    lv_obj_t *item = lv_label_create(logList);
    lv_label_set_text(item, logs[index].c_str());
    lv_obj_set_width(item, 210);
    lv_label_set_long_mode(item, LV_LABEL_LONG_WRAP);
  }
}

void runDiagnostics(lv_event_t *) {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("Connect to Wi-Fi first");
    if (gatewayPingLabel) lv_label_set_text(gatewayPingLabel, "Gateway ICMP: Wi-Fi offline");
    if (dnsPingLabel) lv_label_set_text(dnsPingLabel, "DNS ICMP: Wi-Fi offline");
    addLog("Diagnostics: offline");
    return;
  }

  if (!RadioManager::switchMode(MODE_WIFI_STA) || !DiagnosticsManager::start()) {
    setStatus("Could not start ICMP probes");
    return;
  }
  diagnosticsRunning = true;
  lv_label_set_text(gatewayPingLabel, "Gateway ICMP: probing...");
  lv_label_set_text(dnsPingLabel, "DNS ICMP: probing...");
  setStatus("ICMP probes running");
}

void populateNetworks() {
  if (!networkList) return;
  clearContainer(networkList);
  if (networkCount == 0) {
    lv_obj_t *empty = lv_label_create(networkList);
    lv_label_set_text(empty, "No networks found");
    return;
  }
  for (uint8_t index = 0; index < networkCount; index++) {
    lv_obj_t *row = lv_obj_create(networkList);
    lv_obj_set_size(row, 286, 34);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *nameBox = lv_btn_create(row);
    lv_obj_set_size(nameBox, 204, 29);
    lv_obj_set_style_pad_hor(nameBox, 5, 0);
    lv_obj_add_event_cb(nameBox, [](lv_event_t *event) {
      NetworkInfo *network = static_cast<NetworkInfo *>(lv_event_get_user_data(event));
      if (network->ssid.length() == 0) {
        setStatus("Cannot connect to a hidden network");
        return;
      }
      openPasswordEntry(network->ssid);
    }, LV_EVENT_CLICKED, &networks[index]);
    lv_obj_t *name = lv_label_create(nameBox);
    String ssid = networks[index].ssid.length() ? networks[index].ssid : "Hidden network";
    lv_label_set_text(name, ssid.c_str());
    lv_obj_set_width(name, 190);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_center(name);

    lv_obj_t *signalBox = lv_obj_create(row);
    lv_obj_set_size(signalBox, 74, 29);
    lv_obj_set_style_pad_all(signalBox, 2, 0);
    lv_obj_set_style_border_width(signalBox, 0, 0);
    lv_obj_set_style_bg_color(signalBox, lv_color_hex(0x20313B), 0);
    lv_obj_t *signal = lv_label_create(signalBox);
    const char *security = networks[index].encryption == WIFI_AUTH_OPEN
                               ? "OPEN"
                               : networks[index].encryption == WIFI_AUTH_WEP ? "WEP" : "SECURE";
    lv_label_set_text_fmt(signal, "%d dBm %s", networks[index].rssi, security);
    lv_obj_center(signal);
  }
}

void openPasswordEntry(const String &ssid) {
  if (passwordDialog) lv_obj_del(passwordDialog);
  pendingSsid = ssid;
  passwordDialog = lv_obj_create(lv_layer_top());
  lv_obj_set_size(passwordDialog, ScreenWidth, ScreenHeight);
  lv_obj_set_pos(passwordDialog, 0, 0);
  lv_obj_set_style_bg_color(passwordDialog, themeBackgroundColor(), 0);
  lv_obj_set_style_bg_opa(passwordDialog, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(passwordDialog, 0, 0);
  lv_obj_set_style_pad_all(passwordDialog, 0, 0);
  lv_obj_clear_flag(passwordDialog, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *prompt = lv_label_create(passwordDialog);
  lv_label_set_text_fmt(prompt, "Password for %s", pendingSsid.c_str());
  lv_obj_set_width(prompt, 300);
  lv_label_set_long_mode(prompt, LV_LABEL_LONG_DOT);
  lv_obj_align(prompt, LV_ALIGN_TOP_MID, 0, 5);

  lv_obj_t *password = lv_textarea_create(passwordDialog);
  lv_obj_set_size(password, 300, 32);
  lv_obj_align(password, LV_ALIGN_TOP_MID, 0, 29);
  lv_textarea_set_one_line(password, true);
  lv_textarea_set_password_mode(password, true);
  lv_textarea_set_max_length(password, 63);
  lv_textarea_set_placeholder_text(password, "Wi-Fi password (leave blank if open)");

  lv_obj_t *keyboard = lv_keyboard_create(passwordDialog);
  lv_obj_set_size(keyboard, 310, 170);
  lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(keyboard, password);
  lv_obj_add_event_cb(keyboard, [](lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_READY) {
      lv_obj_t *keyboardObj = lv_event_get_target(event);
      lv_obj_t *textArea = lv_keyboard_get_textarea(keyboardObj);
      String passwordText = lv_textarea_get_text(textArea);
      String ssidToConnect = pendingSsid;
      lv_obj_del_async(passwordDialog);
      passwordDialog = nullptr;
      RadioManager::rememberWifiCredentials(ssidToConnect, passwordText);
      RadioManager::switchMode(MODE_WIFI_STA);
      WiFi.begin(ssidToConnect.c_str(), passwordText.c_str());
      wifiAttemptStarted = millis();
      wifiConnecting = true;
      setStatus("Connecting to Wi-Fi...");
      addLog("Wi-Fi connection requested");
    } else if (lv_event_get_code(event) == LV_EVENT_CANCEL) {
      lv_obj_del_async(passwordDialog);
      passwordDialog = nullptr;
      pendingSsid = "";
      setStatus("Wi-Fi connection cancelled");
    }
  }, LV_EVENT_ALL, nullptr);
  applyThemeToObjectTree(passwordDialog);
}

bool isPossibleTrackerName(const String &name) {
  String loweredName = name;
  loweredName.toLowerCase();
  return loweredName.indexOf("airtag") >= 0 ||
         loweredName.indexOf("tile") >= 0 ||
         loweredName.indexOf("smarttag") >= 0 ||
         loweredName.indexOf("chipolo") >= 0 ||
         loweredName.indexOf("findmy") >= 0;
}

void bleScanComplete(BLEScanResults results) {
  bleDeviceCount = 0;
  const int found = results.getCount();
  for (int index = 0; index < found && bleDeviceCount < MaxBleDevices; index++) {
    BLEAdvertisedDevice device = results.getDevice(index);
    const std::string deviceName = device.getName();
    const std::string address = device.getAddress().toString();
    String name = deviceName.empty() ? "Unnamed BLE device" : String(deviceName.c_str());
    bleDevices[bleDeviceCount++] = {
        name, String(address.c_str()), device.getRSSI(), isPossibleTrackerName(name),
        device.getAddressType()};
  }
  bleScanDone = true;
}

void disconnectBleDevice(lv_event_t *) {
  if (bleClient && bleClient->isConnected()) {
    bleClient->disconnect();
  }
  connectedBleName = "";
  if (connectionStatusLabel) lv_label_set_text(connectionStatusLabel, "Bluetooth: disconnected");
  if (bleDisconnectButton) lv_obj_add_flag(bleDisconnectButton, LV_OBJ_FLAG_HIDDEN);
  setStatus("Bluetooth disconnected");
}

void connectSelectedBle(lv_event_t *event) {
  auto *device = static_cast<BleDeviceInfo *>(lv_event_get_user_data(event));
  if (!device || scanJob != ScanJob::Idle) return;

  if (bleClient && bleClient->isConnected()) bleClient->disconnect();
  connectedBleName = "";
  if (!RadioManager::switchMode(MODE_BLE_SCAN)) {
    if (connectionStatusLabel) lv_label_set_text(connectionStatusLabel, "Bluetooth: radio unavailable");
    setStatus("BLE radio unavailable");
    return;
  }

  if (connectionStatusLabel) lv_label_set_text(connectionStatusLabel, "Bluetooth: connecting...");
  if (bleDisconnectButton) lv_obj_add_flag(bleDisconnectButton, LV_OBJ_FLAG_HIDDEN);
  lv_refr_now(nullptr);
  if (!bleClient) bleClient = BLEDevice::createClient();
  const BLEAddress address(std::string(device->address.c_str()));
  const bool connected = bleClient && bleClient->connect(address, device->addressType);
  if (connected) {
    connectedBleName = device->name;
    if (connectionStatusLabel && scanJob == ScanJob::Idle) {
      lv_label_set_text_fmt(connectionStatusLabel, "Bluetooth: connected to %s",
                            connectedBleName.c_str());
    }
    if (bleDisconnectButton) lv_obj_clear_flag(bleDisconnectButton, LV_OBJ_FLAG_HIDDEN);
    setStatus("Bluetooth connected");
    addLog("BLE connected: " + connectedBleName);
  } else {
    if (connectionStatusLabel) lv_label_set_text(connectionStatusLabel, "Bluetooth: connection failed");
    setStatus("Bluetooth connection failed");
  }
}

void renderBleResults() {
  if (bleDeviceList) clearContainer(bleDeviceList);
  if (bleDeviceCount == 0) {
    if (bleDeviceList) {
      lv_obj_t *empty = lv_label_create(bleDeviceList);
      lv_label_set_text(empty, "No BLE advertisements found");
    }
    setStatus("No BLE devices found");
  } else {
    for (uint8_t index = 0; index < bleDeviceCount; index++) {
      if (!bleDeviceList) break;
      lv_obj_t *item = lv_btn_create(bleDeviceList);
      lv_obj_set_size(item, 284, 42);
      lv_obj_add_event_cb(item, connectSelectedBle, LV_EVENT_CLICKED, &bleDevices[index]);
      lv_obj_t *description = lv_label_create(item);
      lv_label_set_text_fmt(description, "%s  %d dBm%s\n%s",
                            bleDevices[index].name.c_str(), bleDevices[index].rssi,
                            bleDevices[index].possibleTracker ? "  POSSIBLE TRACKER" : "",
                            bleDevices[index].address.c_str());
      lv_obj_set_width(description, 270);
      lv_label_set_long_mode(description, LV_LABEL_LONG_DOT);
      lv_obj_align(description, LV_ALIGN_LEFT_MID, 4, 0);
    }
    setStatus(String(bleDeviceCount) + " BLE devices found; tap one to connect");
  }
  addLog("BLE scan: " + String(bleDeviceCount) + " devices");
}

void startBleScan() {
  if (scanJob != ScanJob::Idle) {
    setStatus("A radio scan is already running");
    return;
  }
  if (!RadioManager::switchMode(MODE_BLE_SCAN)) {
    setStatus("BLE radio unavailable");
    return;
  }
  bleScanDone = false;
  bleDeviceCount = 0;
  BLEScan *scanner = BLEDevice::getScan();
  scanner->setActiveScan(true);
  scanner->setInterval(100);
  scanner->setWindow(80);
  scanJob = ScanJob::Ble;
  if (!scanner->start(BleScanSeconds, bleScanComplete, false)) {
    scanJob = ScanJob::Idle;
    RadioManager::switchMode(MODE_WIFI_STA);
    setStatus("Could not start BLE scan");
  } else {
    setStatus("Scanning for BLE devices...");
  }
}

void finishWifiScan(int found) {
  networkCount = 0;
  for (int index = 0; index < found && networkCount < MaxNetworks; index++) {
    networks[networkCount++] = {
        WiFi.SSID(index), WiFi.RSSI(index), WiFi.channel(index),
        static_cast<uint8_t>(WiFi.encryptionType(index))};
  }
  WiFi.scanDelete();
  populateNetworks();

  if (channelChart && channelSeries) {
    lv_chart_set_all_value(channelChart, channelSeries, LV_CHART_POINT_NONE);
    for (uint8_t channel = 1; channel <= 13; channel++) {
      uint8_t count = 0;
      for (uint8_t index = 0; index < networkCount; index++) {
        if (networks[index].channel == channel) count++;
      }
      lv_chart_set_next_value(channelChart, channelSeries, count);
    }
    lv_chart_refresh(channelChart);
  }
  addLog("Scan: " + String(networkCount) + " networks");
  setStatus(String(networkCount) + " networks found");
  scanJob = ScanJob::Idle;
}

void scanNetworks(lv_event_t *) {
  if (scanJob != ScanJob::Idle) {
    setStatus("A radio scan is already running");
    return;
  }
  if (bleClient && bleClient->isConnected()) bleClient->disconnect();
  connectedBleName = "";
  if (!RadioManager::switchMode(MODE_WIFI_STA)) {
    setStatus("Wi-Fi radio unavailable");
    return;
  }
  setStatus("Scanning nearby networks...");
  networkCount = 0;
  const int result = WiFi.scanNetworks(true, true);
  if (result == WIFI_SCAN_FAILED) {
    setStatus("Could not start Wi-Fi scan");
    return;
  }
  scanJob = ScanJob::Wifi;
}

void scanBleDevices(lv_event_t *) {
  if (bleClient && bleClient->isConnected()) bleClient->disconnect();
  connectedBleName = "";
  startBleScan();
}

void serviceScans() {
  if (scanJob == ScanJob::Wifi) {
    const int result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return;
    if (result < 0) {
      WiFi.scanDelete();
      scanJob = ScanJob::Idle;
      setStatus("Wi-Fi scan failed");
      return;
    }
    scanJob = ScanJob::Idle;
    finishWifiScan(result);
  }

  if (scanJob == ScanJob::Ble && bleScanDone) {
    bleScanDone = false;
    scanJob = ScanJob::Idle;
    BLEDevice::getScan()->clearResults();
    if (!RadioManager::switchMode(MODE_WIFI_STA)) {
      setStatus("Could not restore Wi-Fi radio");
      return;
    }
    renderBleResults();
  }
}

void cancelScansForNavigation() {
  if (scanJob == ScanJob::Wifi) {
    const esp_err_t result = esp_wifi_scan_stop();
    if (result != ESP_OK) {
      addLog("Wi-Fi scan stop returned " + String(static_cast<int>(result)));
    }
    WiFi.scanDelete();
  } else if (scanJob == ScanJob::Ble &&
             BLEDevice::getInitialized()) {
    BLEDevice::getScan()->stop();
  }
  if (bleClient && bleClient->isConnected()) {
    bleClient->disconnect();
    connectedBleName = "";
  }
  scanJob = ScanJob::Idle;
  bleScanDone = false;
}
void toggleDarkMode(lv_event_t *event) {
  darkMode = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
  preferences.putBool("dark_mode", darkMode);
  applyDashboardTheme();
}

void toggleWidsGuard(lv_event_t *event) {
  if (lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED)) {
    if (trafficMonitorActive) {
      TrafficMonitor::stop();
      trafficMonitorActive = false;
      if (trafficToggleSwitch) lv_obj_clear_state(trafficToggleSwitch, LV_STATE_CHECKED);
      if (trafficStatusLabel) lv_label_set_text(trafficStatusLabel, "Stopped: WIDS Guard is using the radio.");
    }
    if (!WidsGuard::start()) {
      widsActive = false;
      lv_obj_clear_state(widsSwitch, LV_STATE_CHECKED);
      if (widsStatusLabel) lv_label_set_text(widsStatusLabel, "Monitor could not start.");
      setStatus("WIDS monitor failed");
      return;
    }
    widsActive = true;
    if (widsStatusLabel) lv_label_set_text(widsStatusLabel, "Monitoring channels 1-13 for deauth bursts and security-mismatched duplicate SSIDs.");
    setStatus("WIDS monitor active");
    addLog("WIDS monitoring started");
  } else {
    WidsGuard::stop();
    widsActive = false;
    if (widsStatusLabel) lv_label_set_text(widsStatusLabel, "Monitor stopped.");
    setStatus("WIDS monitor stopped");
    addLog("WIDS monitoring stopped");
  }
}

void toggleTrafficMonitor(lv_event_t *event) {
  if (lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED)) {
    if (widsActive) {
      WidsGuard::stop();
      widsActive = false;
      if (widsSwitch) lv_obj_clear_state(widsSwitch, LV_STATE_CHECKED);
      if (widsStatusLabel) lv_label_set_text(widsStatusLabel, "Monitor stopped: Traffic Monitor is using the radio.");
    }
    if (!TrafficMonitor::start()) {
      trafficMonitorActive = false;
      lv_obj_clear_state(trafficToggleSwitch, LV_STATE_CHECKED);
      if (trafficStatusLabel) lv_label_set_text(trafficStatusLabel, "Could not start passive capture.");
      setStatus("Traffic monitor failed");
      return;
    }
    trafficMonitorActive = true;
    if (trafficStatusLabel) {
      lv_label_set_text(trafficStatusLabel,
                        "Monitoring nearby 2.4 GHz frames. Wi-Fi disconnects while active.");
    }
    setStatus("Traffic monitor active");
  } else {
    TrafficMonitor::stop();
    trafficMonitorActive = false;
    if (trafficStatusLabel) lv_label_set_text(trafficStatusLabel, "Monitor stopped.");
    setStatus("Traffic monitor stopped");
  }
}

void refreshTrafficMonitor() {
  if (!trafficDetailsLabel || !trafficMonitorActive) return;
  const TrafficMonitor::Snapshot stats = TrafficMonitor::snapshot();
  lv_label_set_text_fmt(trafficDetailsLabel,
                        "Frames seen: %lu\nManagement: %lu  Control: %lu\nData: %lu  Other: %lu\nApprox. observed frame bytes: %llu\nAverage RSSI: %ld dBm  Channel: %u",
                        static_cast<unsigned long>(stats.frames),
                        static_cast<unsigned long>(stats.managementFrames),
                        static_cast<unsigned long>(stats.controlFrames),
                        static_cast<unsigned long>(stats.dataFrames),
                        static_cast<unsigned long>(stats.otherFrames),
                        static_cast<unsigned long long>(stats.observedBytes),
                        static_cast<long>(stats.averageRssi),
                        static_cast<unsigned>(stats.channel));
}

void serviceWidsAlerts() {
  if (widsActive) WidsGuard::service();
  WidsGuard::Alert alert {};
  while (WidsGuard::popAlert(alert)) {
    String message(alert.message);
    addLog("WIDS alert: " + message);
    StorageManager::logWidsAlert(TimeManager::timestamp(), message);
    if (widsStatusLabel) lv_label_set_text(widsStatusLabel, message.c_str());
    setStatus("WIDS ALERT");
  }
}

void refreshSystemStatus() {
  const uint32_t now = millis();
  if (now - lastStatusRefresh < NdtConfig::StatusRefreshMs) return;
  lastStatusRefresh = now;

  if (clockLabel) {
    String clock = TimeManager::dateString() + " " + TimeManager::timeString();
    lv_label_set_text(clockLabel, clock.c_str());
  }
  if (radioStatusLabel) {
    lv_label_set_text_fmt(radioStatusLabel, "RADIO:%s", RadioManager::modeName());
  }
  if (sdStatusLabel) lv_label_set_text(sdStatusLabel, StorageManager::statusText());
  if (ipStatusLabel) {
    String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("offline");
    lv_label_set_text_fmt(ipStatusLabel, "IP:%s", ip.c_str());
  }
  if (connectionStatusLabel) {
    if (connectivityView == ConnectivityView::Wifi) {
      if (wifiConnecting) {
        lv_label_set_text(connectionStatusLabel, "Wi-Fi: connecting...");
      } else if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text_fmt(connectionStatusLabel, "Wi-Fi: connected to %s",
                              WiFi.SSID().c_str());
      } else {
        lv_label_set_text(connectionStatusLabel, "Wi-Fi: not connected");
      }
    } else if (bleClient && bleClient->isConnected()) {
      lv_label_set_text_fmt(connectionStatusLabel, "Bluetooth: connected to %s",
                            connectedBleName.c_str());
      if (bleDisconnectButton) lv_obj_clear_flag(bleDisconnectButton, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(connectionStatusLabel, "Bluetooth: disconnected");
      if (bleDisconnectButton) lv_obj_add_flag(bleDisconnectButton, LV_OBJ_FLAG_HIDDEN);
    }
  }
  refreshTrafficMonitor();

  if (diagnosticsRunning && gatewayPingLabel && dnsPingLabel) {
    DiagnosticsManager::ProbeResult gateway = DiagnosticsManager::gatewayResult();
    DiagnosticsManager::ProbeResult dns = DiagnosticsManager::dnsResult();
    if (gateway.complete && dns.complete) {
      lv_label_set_text_fmt(gatewayPingLabel, "Gateway %s: %u/%u replies, avg %lu ms",
                            gateway.target.c_str(), gateway.replies, gateway.attempts,
                            static_cast<unsigned long>(gateway.averageMs));
      lv_label_set_text_fmt(dnsPingLabel, "DNS %s: %u/%u replies, avg %lu ms",
                            dns.target.c_str(), dns.replies, dns.attempts,
                            static_cast<unsigned long>(dns.averageMs));
      diagnosticsRunning = false;
      setStatus("ICMP diagnostics complete");
      addLog("ICMP gateway " + String(gateway.replies) + "/" + String(gateway.attempts) +
             ", DNS " + String(dns.replies) + "/" + String(dns.attempts));
    }
  }

  if (subnetResultsLabel) {
    if (SubnetScanner::running()) {
      lv_label_set_text_fmt(subnetResultsLabel, "Scanning TCP port 80: %u%%",
                            SubnetScanner::progressPercent());
    } else if (SubnetScanner::complete()) {
      String results = "TCP port 80 hosts: " + SubnetScanner::discoveredHosts();
      lv_label_set_text(subnetResultsLabel, results.c_str());
    }
  }
}

lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_event_cb_t callback) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_height(button, 34);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}

void animateWinkHeight(void *object, int32_t height) {
  lv_obj_set_height(static_cast<lv_obj_t *>(object), height);
}

lv_obj_t *addSplashPixel(lv_obj_t *parent, int16_t x, int16_t y, int16_t size, lv_color_t color) {
  lv_obj_t *pixel = lv_obj_create(parent);
  lv_obj_set_size(pixel, size, size);
  lv_obj_set_pos(pixel, x, y);
  lv_obj_set_style_radius(pixel, 0, 0);
  lv_obj_set_style_bg_color(pixel, color, 0);
  lv_obj_set_style_bg_opa(pixel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(pixel, 0, 0);
  lv_obj_set_style_pad_all(pixel, 0, 0);
  return pixel;
}

void showDashboard(lv_timer_t *timer) {
  if (winkLid) lv_anim_del(winkLid, animateWinkHeight);
  lv_obj_t *oldSplash = splashScreen;
  splashScreen = nullptr;
  winkLid = nullptr;
  createDashboard();
  if (oldSplash) lv_obj_del(oldSplash);
  lv_timer_del(timer);
}

void createSplashScreen() {
  splashScreen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(splashScreen, lv_color_hex(0x101820), 0);
  lv_obj_set_style_border_width(splashScreen, 0, 0);
  lv_scr_load(splashScreen);

  lv_obj_t *title = lv_label_create(splashScreen);
  lv_label_set_text(title, "NDT");
  lv_obj_set_style_text_color(title, lv_color_hex(0x5DE2E7), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

  const lv_color_t eyeColor = lv_color_hex(0xEAF5F7);
  const lv_color_t pixelColor = lv_color_hex(0x101820);
  constexpr int16_t PixelSize = 6;

  for (int16_t x : {116, 122, 128}) addSplashPixel(splashScreen, x, 78, PixelSize, eyeColor);
  for (int16_t x : {110, 116, 122, 128, 134}) {
    addSplashPixel(splashScreen, x, 84, PixelSize, eyeColor);
    addSplashPixel(splashScreen, x, 90, PixelSize, eyeColor);
  }
  for (int16_t x : {116, 122, 128}) addSplashPixel(splashScreen, x, 96, PixelSize, eyeColor);
  for (int16_t x : {122, 128}) {
    addSplashPixel(splashScreen, x, 84, PixelSize, pixelColor);
    addSplashPixel(splashScreen, x, 90, PixelSize, pixelColor);
  }

  for (int16_t x : {182, 188, 194}) addSplashPixel(splashScreen, x, 78, PixelSize, eyeColor);
  for (int16_t x : {176, 182, 188, 194, 200}) {
    addSplashPixel(splashScreen, x, 84, PixelSize, eyeColor);
    addSplashPixel(splashScreen, x, 90, PixelSize, eyeColor);
  }
  for (int16_t x : {182, 188, 194}) addSplashPixel(splashScreen, x, 96, PixelSize, eyeColor);
  for (int16_t x : {188, 194}) {
    addSplashPixel(splashScreen, x, 84, PixelSize, pixelColor);
    addSplashPixel(splashScreen, x, 90, PixelSize, pixelColor);
  }

  winkLid = lv_obj_create(splashScreen);
  lv_obj_set_size(winkLid, 44, 0);
  lv_obj_set_pos(winkLid, 171, 78);
  lv_obj_set_style_bg_color(winkLid, pixelColor, 0);
  lv_obj_set_style_bg_opa(winkLid, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(winkLid, 0, 0);
  lv_obj_set_style_radius(winkLid, 0, 0);
  lv_obj_set_style_pad_all(winkLid, 0, 0);
  addSplashPixel(winkLid, 4, 20, 10, eyeColor);
  addSplashPixel(winkLid, 15, 20, 14, eyeColor);
  addSplashPixel(winkLid, 30, 20, 10, eyeColor);

  const int16_t smilePixels[][2] = {
      {122, 132}, {128, 138}, {134, 144}, {140, 150},
      {146, 150}, {152, 150}, {158, 150}, {164, 150},
      {170, 150}, {176, 144}, {182, 138}, {188, 132}};
  for (const auto &pixel : smilePixels) {
    addSplashPixel(splashScreen, pixel[0], pixel[1], PixelSize, lv_color_hex(0x5DE2E7));
  }

  lv_obj_t *subtitle = lv_label_create(splashScreen);
  lv_label_set_text(subtitle, "Network Diagnostic tool");
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0xA9BBC6), 0);
  lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 60);

  lv_obj_t *credit = lv_label_create(splashScreen);
  lv_label_set_text(credit, "Alexander John Balagso");
  lv_obj_set_style_text_font(credit, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(credit, lv_color_hex(0xA9BBC6), 0);
  lv_obj_align(credit, LV_ALIGN_BOTTOM_MID, 0, -8);

  lv_anim_t winkAnimation;
  lv_anim_init(&winkAnimation);
  lv_anim_set_var(&winkAnimation, winkLid);
  lv_anim_set_exec_cb(&winkAnimation, animateWinkHeight);
  lv_anim_set_values(&winkAnimation, 0, 30);
  lv_anim_set_time(&winkAnimation, 180);
  lv_anim_set_playback_time(&winkAnimation, 180);
  lv_anim_set_repeat_delay(&winkAnimation, 1000);
  lv_anim_set_repeat_count(&winkAnimation, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&winkAnimation);

  lv_timer_create(showDashboard, NdtConfig::SplashDurationMs, nullptr);
}

void createHeader(bool showHomeButton) {
  titleLabel = lv_label_create(screen);
  lv_label_set_text(titleLabel, "NDT");
  lv_obj_set_pos(titleLabel, 8, 2);

  darkModeSwitch = lv_switch_create(screen);
  lv_obj_set_pos(darkModeSwitch, 47, 1);
  lv_obj_set_size(darkModeSwitch, 32, 20);
  if (darkMode) lv_obj_add_state(darkModeSwitch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(darkModeSwitch, toggleDarkMode, LV_EVENT_VALUE_CHANGED, nullptr);

  clockLabel = lv_label_create(screen);
  lv_obj_set_style_text_font(clockLabel, &lv_font_montserrat_10, 0);
  lv_obj_set_width(clockLabel, 190);
  lv_obj_set_style_text_align(clockLabel, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(clockLabel, LV_ALIGN_TOP_RIGHT, -8, 4);

  radioStatusLabel = lv_label_create(screen);
  lv_obj_set_style_text_font(radioStatusLabel, &lv_font_montserrat_10, 0);
  lv_obj_set_pos(radioStatusLabel, 8, 24);
  lv_obj_set_width(radioStatusLabel, 62);

  sdStatusLabel = lv_label_create(screen);
  lv_obj_set_style_text_font(sdStatusLabel, &lv_font_montserrat_10, 0);
  lv_obj_set_pos(sdStatusLabel, 76, 24);
  lv_obj_set_width(sdStatusLabel, 46);

  ipStatusLabel = lv_label_create(screen);
  lv_obj_set_style_text_font(ipStatusLabel, &lv_font_montserrat_10, 0);
  lv_obj_set_pos(ipStatusLabel, 130, 24);
  lv_obj_set_width(ipStatusLabel, showHomeButton ? 118 : 182);
  lv_label_set_long_mode(ipStatusLabel, LV_LABEL_LONG_DOT);

  if (showHomeButton) {
    lv_obj_t *homeButton = makeButton(screen, "HOME", [](lv_event_t *) { createDashboard(); });
    lv_obj_set_size(homeButton, 58, 22);
    lv_obj_align(homeButton, LV_ALIGN_TOP_RIGHT, -6, 20);
  }
}

void createDashboard() {
  cancelScansForNavigation();
  if (trafficMonitorActive) {
    TrafficMonitor::stop();
    trafficMonitorActive = false;
  }
  if (widsActive) {
    WidsGuard::stop();
    widsActive = false;
  }
  RadioManager::switchMode(MODE_WIFI_STA);

  lv_obj_t *previousScreen = screen;
  screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, themeBackgroundColor(), 0);
  lv_obj_set_style_border_width(screen, 0, 0);

  createHeader(false);
  pageBody = lv_obj_create(screen);
  lv_obj_set_size(pageBody, 312, 187);
  lv_obj_set_pos(pageBody, 4, 49);
  lv_obj_set_style_pad_all(pageBody, 4, 0);
  lv_obj_set_style_border_width(pageBody, 0, 0);
  lv_obj_set_scroll_dir(pageBody, LV_DIR_VER);

  statusLabel = nullptr;
  pageStatus = nullptr;
  networkList = nullptr;
  bleDeviceList = nullptr;
  connectionStatusLabel = nullptr;
  bleDisconnectButton = nullptr;
  wifiModeButton = nullptr;
  bleModeButton = nullptr;
  connectivityScanButton = nullptr;
  connectivityScanButtonLabel = nullptr;
  widsSwitch = nullptr;
  widsStatusLabel = nullptr;
  trafficStatusLabel = nullptr;
  trafficToggleSwitch = nullptr;
  trafficDetailsLabel = nullptr;
  subnetResultsLabel = nullptr;
  gatewayPingLabel = nullptr;
  dnsPingLabel = nullptr;
  channelChart = nullptr;
  channelSeries = nullptr;
  latencyLabel = nullptr;
  lossLabel = nullptr;
  logList = nullptr;
  lv_scr_load(screen);
  if (previousScreen) lv_obj_del(previousScreen);

  lv_obj_t *heading = lv_label_create(pageBody);
  lv_label_set_text(heading, "Choose a module");
  lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 0);
  const char *labels[] = {"Diagnostics", "Connections", "Traffic Monitor", "WIDS Guard"};
  lv_event_cb_t callbacks[] = {
      [](lv_event_t *) { createDiagnosticsPage(); },
      [](lv_event_t *) { createConnectivityPage(); },
      [](lv_event_t *) { createTrafficMonitorPage(); },
      [](lv_event_t *) { createWidsPage(); }};
  for (uint8_t index = 0; index < 4; index++) {
    lv_obj_t *button = makeButton(pageBody, labels[index], callbacks[index]);
    lv_obj_set_size(button, 145, 62);
    lv_obj_set_pos(button, 4 + (index % 2) * 153, 30 + (index / 2) * 70);
  }
  applyDashboardTheme();
  refreshSystemStatus();
}

void createDiagnosticsPage() {
  createDashboardBaseForSubpage("Diagnostics");
  makeButton(pageBody, "PING GATEWAY + DNS", runDiagnostics);
  gatewayPingLabel = lv_label_create(pageBody);
  lv_label_set_text(gatewayPingLabel, "Gateway ICMP: not tested");
  dnsPingLabel = lv_label_create(pageBody);
  lv_label_set_text(dnsPingLabel, "DNS ICMP: not tested");
  makeButton(pageBody, "SCAN LOCAL SUBNET", [](lv_event_t *) {
    if (!RadioManager::switchMode(MODE_WIFI_STA) || !SubnetScanner::start()) {
      setStatus("Connect to Wi-Fi before scanning");
      return;
    }
    setStatus("Local subnet scan started");
  });
  subnetResultsLabel = lv_label_create(pageBody);
  lv_obj_set_width(subnetResultsLabel, 290);
  lv_label_set_text(subnetResultsLabel, "Discovers hosts accepting TCP connections on port 80.");
  makeButton(pageBody, "REFRESH LOGS", [](lv_event_t *) { showLogs(nullptr); });
  logList = lv_obj_create(pageBody);
  lv_obj_set_size(logList, 292, 54);
  lv_obj_set_flex_flow(logList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(logList, LV_DIR_VER);
  showLogs(nullptr);
  applyDashboardTheme();
}

void setConnectivityView(ConnectivityView view) {
  if (scanJob != ScanJob::Idle) {
    setStatus("Wait for the current scan to finish");
    return;
  }
  if (connectivityView == ConnectivityView::Bluetooth &&
      view == ConnectivityView::Wifi && bleClient) {
    disconnectBleDevice(nullptr);
    RadioManager::switchMode(MODE_WIFI_STA);
  }

  connectivityView = view;
  if (networkList) {
    if (view == ConnectivityView::Wifi) lv_obj_clear_flag(networkList, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(networkList, LV_OBJ_FLAG_HIDDEN);
  }
  if (bleDeviceList) {
    if (view == ConnectivityView::Bluetooth) lv_obj_clear_flag(bleDeviceList, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(bleDeviceList, LV_OBJ_FLAG_HIDDEN);
  }
  if (connectivityScanButtonLabel) {
    lv_label_set_text(connectivityScanButtonLabel,
                      view == ConnectivityView::Wifi ? "SCAN WI-FI" : "SCAN BLUETOOTH");
  }
  if (wifiModeButton) {
    if (view == ConnectivityView::Wifi) lv_obj_add_state(wifiModeButton, LV_STATE_CHECKED);
    else lv_obj_clear_state(wifiModeButton, LV_STATE_CHECKED);
  }
  if (bleModeButton) {
    if (view == ConnectivityView::Bluetooth) lv_obj_add_state(bleModeButton, LV_STATE_CHECKED);
    else lv_obj_clear_state(bleModeButton, LV_STATE_CHECKED);
  }
  refreshSystemStatus();
}

void createConnectivityPage() {
  createDashboardBaseForSubpage("Connections");
  connectivityView = ConnectivityView::Wifi;
  lv_obj_set_layout(pageBody, 0);
  if (pageStatus) {
    lv_obj_add_flag(pageStatus, LV_OBJ_FLAG_HIDDEN);
    pageStatus = nullptr;
  }
  lv_obj_t *modeRow = lv_obj_create(pageBody);
  lv_obj_set_size(modeRow, 294, 26);
  lv_obj_set_pos(modeRow, 4, 23);
  lv_obj_set_style_pad_all(modeRow, 0, 0);
  lv_obj_set_style_border_width(modeRow, 0, 0);
  lv_obj_set_style_bg_opa(modeRow, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(modeRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(modeRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  wifiModeButton = makeButton(modeRow, "WI-FI", [](lv_event_t *) {
    setConnectivityView(ConnectivityView::Wifi);
  });
  lv_obj_set_size(wifiModeButton, 136, 24);
  bleModeButton = makeButton(modeRow, "BLUETOOTH", [](lv_event_t *) {
    setConnectivityView(ConnectivityView::Bluetooth);
  });
  lv_obj_set_size(bleModeButton, 136, 24);
  lv_obj_add_state(wifiModeButton, LV_STATE_CHECKED);

  connectivityScanButton = makeButton(pageBody, "SCAN WI-FI",
      [](lv_event_t *) {
        if (connectivityView == ConnectivityView::Wifi) scanNetworks(nullptr);
        else scanBleDevices(nullptr);
      });
  lv_obj_set_size(connectivityScanButton, 136, 28);
  lv_obj_set_pos(connectivityScanButton, 4, 53);
  connectivityScanButtonLabel = lv_obj_get_child(connectivityScanButton, 0);
  connectionStatusLabel = lv_label_create(pageBody);
  lv_obj_set_pos(connectionStatusLabel, 4, 86);
  lv_obj_set_width(connectionStatusLabel, 200);
  lv_label_set_long_mode(connectionStatusLabel, LV_LABEL_LONG_DOT);
  lv_label_set_text(connectionStatusLabel, "Wi-Fi: not connected");
  bleDisconnectButton = makeButton(pageBody, "DISCONNECT", disconnectBleDevice);
  lv_obj_set_size(bleDisconnectButton, 100, 24);
  lv_obj_set_pos(bleDisconnectButton, 210, 82);
  lv_obj_add_flag(bleDisconnectButton, LV_OBJ_FLAG_HIDDEN);

  networkList = lv_obj_create(pageBody);
  lv_obj_set_size(networkList, 294, 75);
  lv_obj_set_pos(networkList, 4, 112);
  lv_obj_set_style_pad_all(networkList, 2, 0);
  lv_obj_set_flex_flow(networkList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(networkList, LV_DIR_VER);
  bleDeviceList = lv_obj_create(pageBody);
  lv_obj_set_size(bleDeviceList, 294, 75);
  lv_obj_set_pos(bleDeviceList, 4, 112);
  lv_obj_set_style_pad_all(bleDeviceList, 2, 0);
  lv_obj_set_flex_flow(bleDeviceList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(bleDeviceList, LV_DIR_VER);
  lv_obj_add_flag(bleDeviceList, LV_OBJ_FLAG_HIDDEN);
  applyDashboardTheme();
  refreshSystemStatus();
}

void createTrafficMonitorPage() {
  createDashboardBaseForSubpage("Traffic Monitor");
  trafficToggleSwitch = lv_switch_create(pageBody);
  lv_obj_add_event_cb(trafficToggleSwitch, toggleTrafficMonitor, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t *toggleLabel = lv_label_create(pageBody);
  lv_label_set_text(toggleLabel, "Monitor nearby Wi-Fi frames");
  lv_obj_align_to(toggleLabel, trafficToggleSwitch, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

  trafficStatusLabel = lv_label_create(pageBody);
  lv_obj_set_width(trafficStatusLabel, 288);
  lv_label_set_long_mode(trafficStatusLabel, LV_LABEL_LONG_WRAP);
  lv_label_set_text(trafficStatusLabel, "Stopped. Start monitoring to count frames on channels 1-13.");

  trafficDetailsLabel = lv_label_create(pageBody);
  lv_obj_set_width(trafficDetailsLabel, 288);
  lv_label_set_text(trafficDetailsLabel,
                    "Frames: 0\nManagement: 0  Control: 0\nData: 0  Other: 0\nObserved frame bytes: 0\nAverage RSSI: -- dBm  Channel: --");

  lv_obj_t *limits = lv_label_create(pageBody);
  lv_obj_set_width(limits, 288);
  lv_label_set_long_mode(limits, LV_LABEL_LONG_WRAP);
  lv_label_set_text(limits,
                    "Passive counts only; no payload is saved. Monitoring disconnects Wi-Fi. Accurate internet data usage is unavailable in this firmware.");
  applyDashboardTheme();
}

void createWidsPage() {
  createDashboardBaseForSubpage("WIDS Guard");
  widsSwitch = lv_switch_create(pageBody);
  lv_obj_add_event_cb(widsSwitch, toggleWidsGuard, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t *toggleLabel = lv_label_create(pageBody);
  lv_label_set_text(toggleLabel, "Monitor radio (1-13)");
  lv_obj_align_to(toggleLabel, widsSwitch, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
  widsStatusLabel = lv_label_create(pageBody);
  lv_obj_set_width(widsStatusLabel, 286);
  lv_label_set_long_mode(widsStatusLabel, LV_LABEL_LONG_WRAP);
  lv_label_set_text(widsStatusLabel,
                    "Passive monitoring only. Alerts detect deauthentication bursts and duplicate SSIDs with different privacy settings.");
  applyDashboardTheme();
}

void createDashboardBaseForSubpage(const char *headingText) {
  cancelScansForNavigation();
  if (trafficMonitorActive) {
    TrafficMonitor::stop();
    trafficMonitorActive = false;
  }
  if (widsActive) {
    WidsGuard::stop();
    widsActive = false;
  }
  lv_obj_t *previousScreen = screen;
  screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, themeBackgroundColor(), 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  createHeader(true);
  pageBody = lv_obj_create(screen);
  lv_obj_set_size(pageBody, 312, 187);
  lv_obj_set_pos(pageBody, 4, 49);
  lv_obj_set_style_pad_all(pageBody, 4, 0);
  lv_obj_set_style_border_width(pageBody, 0, 0);
  lv_obj_set_scroll_dir(pageBody, LV_DIR_VER);
  lv_obj_set_flex_flow(pageBody, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pageBody, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *heading = lv_label_create(pageBody);
  lv_label_set_text(heading, headingText);
  lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 0);
  pageStatus = lv_label_create(pageBody);
  lv_label_set_text(pageStatus, "Ready");
  lv_obj_set_width(pageStatus, 290);

  statusLabel = nullptr;
  networkList = bleDeviceList = nullptr;
  connectionStatusLabel = nullptr;
  bleDisconnectButton = nullptr;
  wifiModeButton = bleModeButton = nullptr;
  connectivityScanButton = connectivityScanButtonLabel = nullptr;
  widsSwitch = widsStatusLabel = nullptr;
  trafficStatusLabel = trafficToggleSwitch = trafficDetailsLabel = nullptr;
  subnetResultsLabel = gatewayPingLabel = dnsPingLabel = nullptr;
  channelChart = nullptr;
  channelSeries = nullptr;
  latencyLabel = lossLabel = nullptr;
  logList = nullptr;
  lv_scr_load(screen);
  if (previousScreen) lv_obj_del(previousScreen);
  RadioManager::switchMode(MODE_WIFI_STA);
  applyDashboardTheme();
  refreshSystemStatus();
}

void initializeWiFi() {
  RadioManager::begin();
  WidsGuard::begin();
}

void serviceWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiConnecting) {
      wifiConnecting = false;
      setStatus("Wi-Fi connected");
      addLog("Wi-Fi connected");
    }
    return;
  }

  if (wifiConnecting && millis() - wifiAttemptStarted >= WiFiConnectionTimeoutMs) {
    wifiConnecting = false;
    setStatus("Wi-Fi connection failed");
    addLog("Wi-Fi connection timed out");
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  preferences.begin("wifi-scope", false);
  darkMode = preferences.getBool("dark_mode", true);
  loadLogs();
  TimeManager::begin();
  initializeWiFi();
  pinMode(TftBacklightPin, OUTPUT);
  digitalWrite(TftBacklightPin, HIGH);
  SPI.begin(TouchSckPin, TouchMisoPin, TouchMosiPin, TouchCsPin);
  touch.begin();
  SPI.begin(TouchSckPin, TouchMisoPin, TouchMosiPin, TouchCsPin);
  touch.setRotation(1);
  display.begin();
  display.setRotation(1);
  StorageManager::begin();

  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, buffer, nullptr, ScreenWidth * 16);
  static lv_disp_drv_t displayDriver;
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = ScreenWidth;
  displayDriver.ver_res = ScreenHeight;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  static lv_indev_drv_t touchDriver;
  lv_indev_drv_init(&touchDriver);
  touchDriver.type = LV_INDEV_TYPE_POINTER;
  touchDriver.read_cb = readTouch;
  lv_indev_drv_register(&touchDriver);

  createSplashScreen();
}

void loop() {
  static uint32_t lastTick = millis();
  uint32_t now = millis();
  lv_tick_inc(now - lastTick);
  lastTick = now;
  lv_timer_handler();
  serviceWiFi();
  serviceScans();
  TrafficMonitor::service();
  TimeManager::service(WiFi.status() == WL_CONNECTED);
  SubnetScanner::service();
  serviceWidsAlerts();
  refreshSystemStatus();
  delay(5);
}
