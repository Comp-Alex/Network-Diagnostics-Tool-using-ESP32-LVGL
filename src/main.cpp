#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <lvgl.h>

namespace {
constexpr uint16_t ScreenWidth = 320;
constexpr uint16_t ScreenHeight = 240;
constexpr uint8_t TouchCsPin = 33;
constexpr uint8_t TouchIrqPin = 36;
constexpr uint8_t SpiSckPin = 14;
constexpr uint8_t SpiMisoPin = 12;
constexpr uint8_t SpiMosiPin = 13;
constexpr uint8_t MaxNetworks = 12;
constexpr uint8_t MaxLogEntries = 8;
constexpr uint32_t ScanIntervalMs = 30000;
constexpr uint32_t WiFiConnectionTimeoutMs = 10000;
constexpr uint32_t WiFiRetryIntervalMs = 15000;

TFT_eSPI display;
XPT2046_Touchscreen touch(TouchCsPin, TouchIrqPin);
Preferences preferences;

lv_disp_draw_buf_t drawBuffer;
lv_color_t buffer[ScreenWidth * 20];
lv_obj_t *screen = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *networkList = nullptr;
lv_obj_t *signalBar = nullptr;
lv_obj_t *channelChart = nullptr;
lv_obj_t *latencyLabel = nullptr;
lv_obj_t *lossLabel = nullptr;
lv_obj_t *logList = nullptr;
lv_chart_series_t *channelSeries = nullptr;

struct NetworkInfo {
  String ssid;
  int32_t rssi;
  int32_t channel;
};

NetworkInfo networks[MaxNetworks];
uint8_t networkCount = 0;
String logs[MaxLogEntries];
uint8_t logCount = 0;
uint32_t lastScan = 0;
uint32_t wifiAttemptStarted = 0;
uint32_t nextWifiAttempt = 0;
bool wifiConnecting = false;

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
    lv_obj_set_size(row, 210, 34);
    lv_obj_set_style_pad_all(row, 5, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *name = lv_label_create(row);
    String ssid = networks[index].ssid.length() ? networks[index].ssid : "Hidden network";
    lv_label_set_text(name, ssid.c_str());
    lv_label_set_width(name, 125);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_t *signal = lv_label_create(row);
    lv_label_set_text_fmt(signal, "%d dBm  CH %d", networks[index].rssi, networks[index].channel);
  }
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
  lastScan = millis();
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

void createDashboard() {
  screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
  lv_scr_load(screen);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "WIFI SCOPE");
  lv_obj_set_style_text_color(title, lv_color_hex(0x5DE2E7), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

  statusLabel = lv_label_create(screen);
  lv_label_set_text(statusLabel, "Ready");
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xA9BBC6), 0);
  lv_obj_align(statusLabel, LV_ALIGN_TOP_RIGHT, -12, 12);

  lv_obj_t *tabs = lv_tabview_create(screen, LV_DIR_BOTTOM, 38);
  lv_obj_set_size(tabs, 320, 190);
  lv_obj_align(tabs, LV_ALIGN_TOP_MID, 0, 42);

  lv_obj_t *scanTab = lv_tabview_add_tab(tabs, "SCAN");
  lv_obj_t *diagTab = lv_tabview_add_tab(tabs, "TEST");
  lv_obj_t *logsTab = lv_tabview_add_tab(tabs, "LOGS");
  lv_obj_set_flex_flow(scanTab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(scanTab, 8, 0);
  makeButton(scanTab, "SCAN NETWORKS", scanNetworks);
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
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin("NDT-Alex", "8888-8888");
  setStatus("Connecting to Wi-Fi...");
  wifiAttemptStarted = millis();
  wifiConnecting = true;
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
    nextWifiAttempt = millis() + WiFiRetryIntervalMs;
  }

  if (!wifiConnecting && millis() >= nextWifiAttempt) {
    connectWiFi();
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  preferences.begin("wifi-scope", false);
  loadLogs();
  SPI.begin(SpiSckPin, SpiMisoPin, SpiMosiPin, TouchCsPin);
  display.begin();
  display.setRotation(1);
  touch.begin();
  touch.setRotation(1);

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

  createDashboard();
  connectWiFi();
}

void loop() {
  static uint32_t lastTick = millis();
  uint32_t now = millis();
  lv_tick_inc(now - lastTick);
  lastTick = now;
  lv_timer_handler();
  serviceWiFi();
  if (WiFi.status() == WL_CONNECTED && millis() - lastScan > ScanIntervalMs && networkCount == 0) {
    scanNetworks(nullptr);
  }
  delay(5);
}
