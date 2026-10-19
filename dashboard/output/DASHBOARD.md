# ESP32 Engineering Dashboard

_Generated: 2026-10-19T13:26:58.029951Z_

## Current functionality

# Network Diagnostics Tool (NDT)
Touchscreen network diagnostics and passive wireless monitoring for the
ESP32-2432S028R (CYD). The firmware is built with PlatformIO, Arduino-ESP32,
LVGL 8, TFT_eSPI, and XPT2046 touch.
## Dashboard
The eight-second startup animation shows the NDT title, pixel-art winking
smiley, “Network Diagnostic tool” subtitle, and author credit. The dashboard
then opens with four modules: **Diagnostics**, **Connections**, **Traffic
Monitor**, and **WIDS Guard**. Each module has a Home button. The unlabeled
switch beside NDT changes the persistent theme; dark green/black mode is
enabled by default.
The top status area refreshes once per second with the local date and time,

### Detected modules and responsibilities

- **Config** — files: 1; namespaces: NdtConfig; example functions: —
- **DiagnosticsManager** — files: 2; namespaces: namespace, DiagnosticsManager; example functions: onPingReply, onPingEnd, startProbe, start
- **RadioManager** — files: 2; namespaces: namespace, RadioManager; example functions: isBleMode, stopBleStack, connectRememberedWifi, begin, switchMode, rememberWifiCredentials
- **StorageManager** — files: 2; namespaces: namespace, StorageManager; example functions: restoreTouchBus, mountCardBus, csvEscape, begin, available, logWidsAlert
- **SubnetScanner** — files: 2; namespaces: namespace, SubnetScanner; example functions: addressValue, startScan, start, startDeviceDiscovery, cancel, service
- **TimeManager** — files: 2; namespaces: namespace, TimeManager; example functions: fromBcd, toBcd, readRtc, writeRtc, getCurrentTime, seedSystemClockFromRtc
- **TrafficMonitor** — files: 2; namespaces: namespace, TrafficMonitor; example functions: receiveFrame, start, stop, service, running
- **WidsGuard** — files: 2; namespaces: namespace, WidsGuard; example functions: queueAlert, formatMac, checkBeacon, receiveFrame, begin, start
- **main** — files: 1; namespaces: void; example functions: createDashboard, createDiagnosticsPage, createTrafficMonitorPage, createWidsPage, createConnectivityPage, createDashboardBaseForSubpage

## Project architecture

Architecture summary (auto-extracted):

- MCU: ESP32 (Arduino framework)
- UI: LVGL + TFT_eSPI + XPT2046 touch
- Key services: Diagnostics (ICMP), SubnetScanner (TCP checks), TrafficMonitor (promiscuous frames), WIDS guard (alerts), Storage (SD/NVS), Time (NTP / optional RTC)

### Pin configuration (from src/Config.h)
- constexpr uint8_t TftBacklightPin = 21;
- constexpr uint8_t TouchSckPin = 25;
- constexpr uint8_t TouchMisoPin = 39;
- constexpr uint8_t TouchMosiPin = 32;
- constexpr uint8_t TouchCsPin = 33;
- constexpr uint8_t TouchIrqPin = 36;
- constexpr uint8_t RtcSclPin = 22;
- constexpr uint8_t RtcSdaPin = 27;
- constexpr uint8_t SdCsPin = 5;
- constexpr uint8_t SdSckPin = 18;
- constexpr uint8_t SdMisoPin = 19;
- constexpr uint8_t SdMosiPin = 23;
- constexpr uint8_t AlertSpeakerPin = 26;

## Development roadmap

Proposed roadmap (auto-generated suggestions):

- [ ] Polish Wi‑Fi connection persistence and credential storage (NVS)
- [ ] Add more precise LAN device discovery and optional mDNS support
- [ ] Improve false-positive handling for WIDS heuristics and add configurable sensitivity
- [ ] Add optional packet capture/logging mode (user opt-in) and secure export
- [ ] Add OTA update support and CI build artifacts
- [ ] Implement automated tests or hardware-in-loop validation flows

## Feature checklist

- [x] Diagnostics (ICMP gateway/DNS probes)
- [x] Local subnet TCP discovery
- [x] Traffic Monitor (passive 2.4 GHz)
- [x] WIDS Guard (management-frame watch)
- [x] Touchscreen LVGL UI
- [x] SD card logging
- [x] NTP / optional RTC

## Progress tracker

- Implemented features: 7/7 (100%)
- TODO items found in code: 0

### Recent TODOs
- None found

## Git / GitHub metrics

- Current branch: main
- Last commit: 2a54ae2 2026-10-14 09:17:17 +0800 Improve Wi-Fi scans and add LAN discovery
- Commit count: 6
- Contributors (top):
  - 51	Alex <Alexanderjohnbalagso8@gmail.com>
  - 1	Alexander John Balagso <Alexanderjohnbalagso8@gmail.com>
- Remote origin: https://github.com/Comp-Alex/Network-Diagnostics-Tool-using-ESP32-LVGL.git
- GitHub API metrics not available (GITHUB_TOKEN not set or request failed)