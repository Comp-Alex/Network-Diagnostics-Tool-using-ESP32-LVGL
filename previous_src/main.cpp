#include <Arduino.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <lvgl.h>

namespace {
constexpr uint16_t ScreenWidth = 320;
constexpr uint16_t ScreenHeight = 240;
constexpr uint8_t TftBacklightPin = 21;
constexpr uint8_t TouchSckPin = 25;
constexpr uint8_t TouchMisoPin = 39;
constexpr uint8_t TouchMosiPin = 32;
constexpr uint8_t TouchCsPin = 33;
constexpr uint8_t TouchIrqPin = 36;
constexpr uint8_t MaxNetworks = 12;
constexpr uint8_t MaxBleDevices = 12;
constexpr uint8_t MaxLogEntries = 8;
constexpr uint32_t BleScanSeconds = 5;
constexpr uint32_t WiFiConnectionTimeoutMs = 10000;
constexpr uint32_t ExtenderStatusRefreshMs = 1000;
constexpr uint32_t SplashDurationMs = 4000;

TFT_eSPI display;
XPT2046_Touchscreen touch(TouchCsPin, TouchIrqPin);
Preferences preferences;

lv_disp_draw_buf_t drawBuffer;
lv_color_t buffer[ScreenWidth * 20];
lv_obj_t *screen = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *networkList = nullptr;
lv_obj_t *bleDeviceList = nullptr;
lv_obj_t *extensionWifiStatus = nullptr;
lv_obj_t *extensionBleStatus = nullptr;
lv_obj_t *passwordDialog = nullptr;
lv_obj_t *wifiHotspotSwitch = nullptr;
lv_obj_t *bleBeaconSwitch = nullptr;
lv_obj_t *darkModeSwitch = nullptr;
lv_obj_t *splashScreen = nullptr;
lv_obj_t *winkLid = nullptr;
lv_obj_t *signalBar = nullptr;
lv_obj_t *channelChart = nullptr;
lv_obj_t *latencyLabel = nullptr;
lv_obj_t *lossLabel = nullptr;
lv_obj_t *logList = nullptr;
lv_chart_series_t *channelSeries = nullptr;

void createDashboard();

struct NetworkInfo {
  String ssid;
  int32_t rssi;
  int32_t channel;
};

struct BleDeviceInfo {
  String name;
  String address;
  int32_t rssi;
};

NetworkInfo networks[MaxNetworks];
BleDeviceInfo bleDevices[MaxBleDevices];
uint8_t networkCount = 0;
uint8_t bleDeviceCount = 0;
String logs[MaxLogEntries];
uint8_t logCount = 0;
uint32_t wifiAttemptStarted = 0;
bool wifiConnecting = false;
bool bleInitialized = false;
bool wifiHotspotRunning = false;
bool bleRelayRunning = false;
bool darkMode = true;
String pendingSsid;
String wifiHotspotPassword;
uint32_t lastExtenderStatusRefresh = 0;

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
  lv_obj_set_style_text_color(titleLabel, themeAccentColor(), LV_PART_MAIN);
  lv_obj_set_style_text_color(statusLabel, themeTextColor(), LV_PART_MAIN);
  lv_obj_set_style_text_color(latencyLabel, themeAccentColor(), LV_PART_MAIN);
  lv_obj_set_style_text_color(lossLabel, themeAccentColor(), LV_PART_MAIN);
  lv_chart_set_series_color(channelChart, channelSeries, themeAccentColor());
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
  String entry = String(millis() / 1000) + "s  " + message;
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
  if (statusLabel) lv_label_set_text(statusLabel, message.c_str());
}

void clearContainer(lv_obj_t *container) {
  lv_obj_clean(container);
}

void showLogs(lv_event_t *) {
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
    lv_label_set_text(latencyLabel, "-- ms");
    lv_label_set_text(lossLabel, "100%");
    addLog("Diagnostics: offline");
    return;
  }

  setStatus("Testing 1.1.1.1 ...");
  uint8_t failures = 0;
  uint32_t totalLatency = 0;
  for (uint8_t attempt = 0; attempt < 5; attempt++) {
    WiFiClient client;
    uint32_t started = millis();
    if (client.connect(IPAddress(1, 1, 1, 1), 80, 1500)) {
      totalLatency += millis() - started;
      client.stop();
    } else {
      failures++;
    }
  }

  uint8_t loss = failures * 20;
  uint32_t average = failures == 5 ? 0 : totalLatency / (5 - failures);
  String latency = failures == 5 ? "-- ms" : String(average) + " ms";
  lv_label_set_text(latencyLabel, latency.c_str());
  lv_label_set_text_fmt(lossLabel, "%u%%", loss);
  setStatus("Diagnostics complete");
  addLog("Ping " + latency + ", loss " + String(loss) + "%");
}

void populateNetworks() {
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
    lv_label_set_text_fmt(signal, "%d dBm", networks[index].rssi);
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

void scanNetworks(lv_event_t *) {
  setStatus("Scanning nearby networks...");
  networkCount = 0;
  int found = WiFi.scanNetworks(false, true);
  for (int index = 0; index < found && networkCount < MaxNetworks; index++) {
    networks[networkCount++] = {WiFi.SSID(index), WiFi.RSSI(index), WiFi.channel(index)};
  }
  WiFi.scanDelete();
  populateNetworks();

  lv_chart_set_all_value(channelChart, channelSeries, LV_CHART_POINT_NONE);
  for (uint8_t channel = 1; channel <= 13; channel++) {
    uint8_t count = 0;
    for (uint8_t index = 0; index < networkCount; index++) if (networks[index].channel == channel) count++;
    lv_chart_set_next_value(channelChart, channelSeries, count);
  }
  lv_chart_refresh(channelChart);
  setStatus(String(networkCount) + " networks found");
  addLog("Scan: " + String(networkCount) + " networks");
}

void scanBleDevices(lv_event_t *) {
  setStatus("Scanning for BLE devices...");
  clearContainer(bleDeviceList);
  bleDeviceCount = 0;
  if (!bleInitialized) {
    BLEDevice::init("");
    bleInitialized = true;
  }
  if (bleRelayRunning) BLEDevice::getAdvertising()->stop();

  BLEScan *scanner = BLEDevice::getScan();
  scanner->setActiveScan(true);
  scanner->setInterval(100);
  scanner->setWindow(80);
  BLEScanResults results = scanner->start(BleScanSeconds, false);
  int found = results.getCount();
  for (int index = 0; index < found && bleDeviceCount < MaxBleDevices; index++) {
    BLEAdvertisedDevice device = results.getDevice(index);
    std::string deviceName = device.getName();
    std::string address = device.getAddress().toString();
    bleDevices[bleDeviceCount++] = {
        deviceName.empty() ? String("Unnamed BLE device") : String(deviceName.c_str()),
        String(address.c_str()),
        device.getRSSI()
    };
  }
  scanner->clearResults();

  if (bleDeviceCount == 0) {
    lv_obj_t *empty = lv_label_create(bleDeviceList);
    lv_label_set_text(empty, "No BLE advertisements found");
    setStatus("No BLE devices found");
  } else {
    for (uint8_t index = 0; index < bleDeviceCount; index++) {
      lv_obj_t *item = lv_label_create(bleDeviceList);
      lv_label_set_text_fmt(item, "%s  %d dBm\n%s",
                            bleDevices[index].name.c_str(),
                            bleDevices[index].rssi,
                            bleDevices[index].address.c_str());
      lv_obj_set_width(item, 280);
      lv_label_set_long_mode(item, LV_LABEL_LONG_DOT);
    }
    setStatus(String(bleDeviceCount) + " BLE devices found");
  }
  addLog("BLE scan: " + String(bleDeviceCount) + " devices");
}

void startWifiHotspot(lv_event_t *) {
  if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(extensionWifiStatus, "Connect to upstream Wi-Fi first.");
    setStatus("Connect to Wi-Fi first");
    return;
  }

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  wifiHotspotPassword = "Ndt-" + mac.substring(mac.length() - 6) + "!";
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP("NDT-EXTENDER", wifiHotspotPassword.c_str(), WiFi.channel(), false, 4)) {
    lv_label_set_text(extensionWifiStatus, "Could not start the Wi-Fi access point.");
    setStatus("Hotspot start failed");
    addLog("Wi-Fi hotspot failed to start");
    return;
  }

  wifiHotspotRunning = true;
  setStatus("Wi-Fi hotspot started");
  addLog("Wi-Fi hotspot started");
}

void stopWifiHotspot(lv_event_t *) {
  if (!wifiHotspotRunning) return;
  WiFi.softAPdisconnect(false);
  wifiHotspotRunning = false;
  wifiHotspotPassword = "";
  lv_label_set_text(extensionWifiStatus, "Hotspot stopped.");
  setStatus("Wi-Fi hotspot stopped");
  addLog("Wi-Fi hotspot stopped");
}

void toggleWifiHotspot(lv_event_t *event) {
  if (lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED)) {
    startWifiHotspot(nullptr);
    if (!wifiHotspotRunning) {
      lv_obj_clear_state(wifiHotspotSwitch, LV_STATE_CHECKED);
    }
  } else {
    stopWifiHotspot(nullptr);
  }
}

void startBleRelayBeacon(lv_event_t *) {
  if (!bleInitialized) {
    BLEDevice::init("");
    bleInitialized = true;
  }

  BLEScan *scanner = BLEDevice::getScan();
  scanner->setActiveScan(true);
  scanner->setInterval(100);
  scanner->setWindow(80);
  BLEScanResults results = scanner->start(BleScanSeconds, false);
  const int nearbyDevices = results.getCount();
  scanner->clearResults();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->stop();
  BLEAdvertisementData advertisementData;
  advertisementData.setName("NDT-BLE-RELAY");
  advertisementData.setCompleteServices(BLEUUID("12345678-1234-1234-1234-1234567890ab"));
  advertising->setAdvertisementData(advertisementData);
  advertising->start();
  bleRelayRunning = true;
  lv_label_set_text_fmt(extensionBleStatus,
                        "Beacon: NDT-BLE-RELAY\nNearby advertisers seen: %d\nThis beacon does not forward their data.",
                        nearbyDevices);
  setStatus("BLE beacon active");
  addLog("BLE beacon started; nearby: " + String(nearbyDevices));
}

void stopBleRelayBeacon(lv_event_t *) {
  if (!bleInitialized || !bleRelayRunning) return;
  BLEDevice::getAdvertising()->stop();
  bleRelayRunning = false;
  lv_label_set_text(extensionBleStatus, "BLE beacon stopped.");
  setStatus("BLE beacon stopped");
  addLog("BLE beacon stopped");
}

void toggleBleBeacon(lv_event_t *event) {
  if (lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED)) {
    startBleRelayBeacon(nullptr);
  } else {
    stopBleRelayBeacon(nullptr);
  }
}

void toggleDarkMode(lv_event_t *event) {
  darkMode = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
  preferences.putBool("dark_mode", darkMode);
  applyDashboardTheme();
}

void refreshExtensionStatus() {
  const uint32_t now = millis();
  if (now - lastExtenderStatusRefresh < ExtenderStatusRefreshMs) return;
  lastExtenderStatusRefresh = now;

  if (wifiHotspotRunning && extensionWifiStatus) {
    lv_label_set_text_fmt(extensionWifiStatus,
                          "Hotspot: NDT-EXTENDER\nPassword: %s\nIP: %s\nClients: %u\nInternet forwarding unavailable in this firmware.",
                          wifiHotspotPassword.c_str(),
                          WiFi.softAPIP().toString().c_str(),
                          WiFi.softAPgetStationNum());
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

  lv_timer_create(showDashboard, SplashDurationMs, nullptr);
}

void createDashboard() {
  screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
  lv_scr_load(screen);

  titleLabel = lv_label_create(screen);
  lv_label_set_text(titleLabel, "NDT");
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x5DE2E7), 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 12, 2);
  darkModeSwitch = lv_switch_create(screen);
  lv_obj_set_pos(darkModeSwitch, 52, 4);
  lv_obj_set_size(darkModeSwitch, 36, 22);
  if (darkMode) lv_obj_add_state(darkModeSwitch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(darkModeSwitch, toggleDarkMode, LV_EVENT_VALUE_CHANGED, nullptr);

  statusLabel = lv_label_create(screen);
  lv_label_set_text(statusLabel, "Ready");
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xA9BBC6), 0);
  lv_obj_set_width(statusLabel, 155);
  lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(statusLabel, LV_ALIGN_TOP_RIGHT, -12, 7);
  lv_obj_t *tabs = lv_tabview_create(screen, LV_DIR_BOTTOM, 38);
  lv_obj_set_size(tabs, 320, 200);
  lv_obj_align(tabs, LV_ALIGN_TOP_MID, 0, 30);

  lv_obj_t *scanTab = lv_tabview_add_tab(tabs, "SCAN");
  lv_obj_t *bleTab = lv_tabview_add_tab(tabs, "BLE");
  lv_obj_t *extendTab = lv_tabview_add_tab(tabs, "EXTEND");
  lv_obj_t *diagTab = lv_tabview_add_tab(tabs, "TEST");
  lv_obj_t *logsTab = lv_tabview_add_tab(tabs, "LOGS");
  lv_obj_set_flex_flow(scanTab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(scanTab, 8, 0);
  makeButton(scanTab, "SCAN WI-FI", scanNetworks);
  networkList = lv_obj_create(scanTab);
  lv_obj_set_size(networkList, 300, 92);
  lv_obj_set_style_pad_all(networkList, 4, 0);
  lv_obj_set_flex_flow(networkList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(networkList, LV_DIR_VER);

  channelChart = lv_chart_create(scanTab);
  lv_obj_set_size(channelChart, 300, 45);
  lv_chart_set_type(channelChart, LV_CHART_TYPE_BAR);
  lv_chart_set_point_count(channelChart, 13);
  lv_chart_set_range(channelChart, LV_CHART_AXIS_PRIMARY_Y, 0, 8);
  channelSeries = lv_chart_add_series(channelChart, lv_color_hex(0xFFB547), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_value(channelChart, channelSeries, 0);
  lv_chart_refresh(channelChart);

  lv_obj_set_flex_flow(bleTab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(bleTab, 8, 0);
  makeButton(bleTab, "SCAN BLUETOOTH LE", scanBleDevices);
  bleDeviceList = lv_obj_create(bleTab);
  lv_obj_set_size(bleDeviceList, 300, 115);
  lv_obj_set_style_pad_all(bleDeviceList, 5, 0);
  lv_obj_set_flex_flow(bleDeviceList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(bleDeviceList, LV_DIR_VER);
  lv_label_set_text(lv_label_create(bleDeviceList), "BLE scan results appear here");

  lv_obj_set_flex_flow(extendTab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(extendTab, 6, 0);
  lv_obj_t *wifiSection = lv_label_create(extendTab);
  lv_label_set_text(wifiSection, "WI-FI ACCESS POINT");
  extensionWifiStatus = lv_label_create(extendTab);
  lv_label_set_text(extensionWifiStatus,
                    "Connect upstream from SCAN first. This firmware cannot forward internet to hotspot clients.");
  lv_obj_set_width(extensionWifiStatus, 290);
  lv_label_set_long_mode(extensionWifiStatus, LV_LABEL_LONG_WRAP);
  lv_obj_t *wifiControlRow = lv_obj_create(extendTab);
  lv_obj_set_size(wifiControlRow, 290, 32);
  lv_obj_set_style_pad_all(wifiControlRow, 2, 0);
  lv_obj_set_style_border_width(wifiControlRow, 0, 0);
  lv_obj_set_style_bg_opa(wifiControlRow, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(wifiControlRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(wifiControlRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_label_set_text(lv_label_create(wifiControlRow), "Hotspot");
  wifiHotspotSwitch = lv_switch_create(wifiControlRow);
  lv_obj_add_event_cb(wifiHotspotSwitch, toggleWifiHotspot, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *bleSection = lv_label_create(extendTab);
  lv_label_set_text(bleSection, "BLUETOOTH BEACON");
  extensionBleStatus = lv_label_create(extendTab);
  lv_label_set_text(extensionBleStatus,
                    "Scans nearby advertisers and broadcasts an NDT beacon. It does not forward other devices' data.");
  lv_obj_set_width(extensionBleStatus, 290);
  lv_label_set_long_mode(extensionBleStatus, LV_LABEL_LONG_WRAP);
  lv_obj_t *bleControlRow = lv_obj_create(extendTab);
  lv_obj_set_size(bleControlRow, 290, 32);
  lv_obj_set_style_pad_all(bleControlRow, 2, 0);
  lv_obj_set_style_border_width(bleControlRow, 0, 0);
  lv_obj_set_style_bg_opa(bleControlRow, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(bleControlRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bleControlRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_label_set_text(lv_label_create(bleControlRow), "BLE beacon");
  bleBeaconSwitch = lv_switch_create(bleControlRow);
  lv_obj_add_event_cb(bleBeaconSwitch, toggleBleBeacon, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_set_flex_flow(diagTab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(diagTab, 12, 0);
  makeButton(diagTab, "RUN DIAGNOSTICS", runDiagnostics);
  latencyLabel = lv_label_create(diagTab);
  lv_label_set_text(latencyLabel, "-- ms");
  lv_obj_set_style_text_color(latencyLabel, lv_color_hex(0x5DE2E7), 0);
  lossLabel = lv_label_create(diagTab);
  lv_label_set_text(lossLabel, "--%");
  lv_obj_set_style_text_color(lossLabel, lv_color_hex(0xFFB547), 0);
  lv_label_set_text(lv_label_create(diagTab), "latency     packet loss");

  lv_obj_set_flex_flow(logsTab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(logsTab, 8, 0);
  makeButton(logsTab, "REFRESH LOGS", [](lv_event_t *) { showLogs(nullptr); });
  logList = lv_obj_create(logsTab);
  lv_obj_set_size(logList, 300, 125);
  lv_obj_set_style_pad_all(logList, 5, 0);
  lv_obj_set_flex_flow(logList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(logList, LV_DIR_VER);
  showLogs(nullptr);

  applyDashboardTheme();
}

void initializeWiFi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
}

void serviceWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiConnecting) {
      wifiConnecting = false;
      setStatus("Wi-Fi connected");
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
  initializeWiFi();
  pinMode(TftBacklightPin, OUTPUT);
  digitalWrite(TftBacklightPin, HIGH);
  SPI.begin(TouchSckPin, TouchMisoPin, TouchMosiPin, TouchCsPin);
  touch.begin();
  SPI.begin(TouchSckPin, TouchMisoPin, TouchMosiPin, TouchCsPin);
  touch.setRotation(1);
  display.begin();
  display.setRotation(1);

  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, buffer, nullptr, ScreenWidth * 20);
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
  refreshExtensionStatus();
  delay(5);
}
