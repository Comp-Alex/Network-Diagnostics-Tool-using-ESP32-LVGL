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
radio mode, SD-card status, and Wi-Fi IP address. Time is synchronized through
NTP after Wi-Fi connects and continues from the ESP32 system clock offline. If
a DS3231 is present, its time seeds the system clock at boot and is updated
after a successful NTP sync. The configured timezone is UTC+8.

## Modules and limitations

### Diagnostics

- ICMP probes run asynchronously against the current gateway and DNS server
  (falling back to `8.8.8.8` when no DNS address is configured); each target
  receives three probes.
- The local subnet scanner incrementally tests TCP port 80. It is not a
  general host or port scanner.
- The on-screen log retains the newest eight entries in ESP32 NVS.

### Connections

This is the single place to discover and connect to nearby Wi-Fi and BLE
devices. Choose Wi-Fi or Bluetooth, then scan. Wi-Fi results show signal and
security; selecting a network opens password entry and connection status.
Bluetooth results show name, RSSI, and address; selecting a device attempts a
BLE GATT connection and reports connected/disconnected status only. It does
not read or write device services or characteristics. Wi-Fi and BLE use the
same radio and cannot remain connected at the same time.

### Traffic Monitor

The passive monitor counts nearby 2.4 GHz Wi-Fi management, control, and data
frames while hopping channels 1–13. It displays frame counts, approximate
observed frame bytes, and average RSSI. It does not save or decode packet
payloads. This is an over-the-air observation, not a measure of the connected
network's or device's internet usage. The ESP32's single radio disconnects
from Wi-Fi while monitoring; stop the monitor to resume station operation.
The Traffic Monitor screen also has a LAN IP scan. It checks the local subnet
for hosts responding on TCP ports 80, 443, 22, 445, or 8080 and displays their
IP addresses and the responding port. This is best-effort host discovery, not
the router's DHCP client list: devices with those ports closed or filtered may
not appear. Starting the LAN scan stops passive frame monitoring and uses the
ESP32's Wi-Fi connection.

### WIDS Guard

This is passive management-frame monitoring, not packet injection. It watches
Wi-Fi channels 1–13 for deauthentication/disassociation bursts and duplicate
SSID beacons with mismatched privacy bits. These are alert heuristics and can
produce false positives. Alerts are shown on screen and appended to
`/wids_alerts.csv` when an SD card is available; the speaker alert uses GPIO
26.

Wi-Fi scan results in Connections identify visible open/WEP networks. BLE
device names matching strings such as “AirTag”, “Tile”, “SmartTag”, or
“Chipolo” are marked as possible tracker-like names; this heuristic does not
reliably identify trackers. Scans run asynchronously so the display clock and
status continue to update. Wi-Fi scans dwell for 800 ms per channel and are
limited to 20 seconds; timed-out or failed scans release their radio resources
and report the driver result code.
See [the Wi-Fi scan fix report](./WIFI_SCAN_FIX_REPORT.md) for details.

## Source layout

```text
src/
├── Config.h                 Hardware pins, timing, and shared constants
├── DiagnosticsManager.cpp/.h Asynchronous gateway and DNS ICMP probes
├── RadioManager.cpp/.h      Wi-Fi/BLE/promiscuous radio mode transitions
├── StorageManager.cpp/.h    SD-card status and WIDS CSV logging
├── SubnetScanner.cpp/.h     Incremental TCP port-80 subnet scan
├── TimeManager.cpp/.h       NTP, optional DS3231, and time formatting
├── TrafficMonitor.cpp/.h    Passive Wi-Fi frame counters
├── WidsGuard.cpp/.h         Passive Wi-Fi management-frame alerts
└── main.cpp                 LVGL screens, touch, and application services
```

`previous_src/main.cpp` is an archived source snapshot. It is outside the
PlatformIO `src/` compile tree.

## Hardware and pin assignments

The pin map below reflects the project's current CYD configuration. Board
revisions and attached modules can differ; verify the board schematic before
connecting external hardware.

| Function | GPIO |
| --- | ---: |
| TFT SCK / MISO / MOSI | 14 / 12 / 13 |
| TFT CS / DC / reset / backlight | 15 / 2 / -1 / 21 |
| Touch SCK / MISO / MOSI | 25 / 39 / 32 |
| Touch CS / interrupt | 33 / 36 |
| Optional DS3231 SDA / SCL (CN1) | 27 / 22 |
| Assumed SD CS / SCK / MISO / MOSI | 5 / 18 / 19 / 23 |
| Alert speaker | 26 |

The display and touch controller use separate SPI configurations. The SD
mapping is an assumption for the supported board arrangement; card access
temporarily switches the shared SPI bus and restores the touch pin mapping.

## Build and upload

1. Install VS Code with PlatformIO and open this project.
2. Build with `pio run`.
3. Upload with `pio run -t upload`; open the serial monitor at 115200 baud.

The project uses the ESP32 `huge_app.csv` partition layout for the larger
application image. If upload reports “Wrong boot mode detected,” hold BOOT,
tap EN/RESET, then retry the upload.

Touch calibration is defined in `readTouch()` in `src/main.cpp`. If touch is
offset or reversed on a board revision, adjust its raw bounds or touch
rotation.

## Notes

- Wi-Fi credentials are held in RAM for the current boot and are not written
  to NVS. Re-enter them after reboot.
- Wi-Fi scanning and BLE scanning are asynchronous. Passive Traffic Monitor
  and WIDS Guard are mutually exclusive and disconnect station Wi-Fi while
  running; stopping either returns the radio to station mode.
- A missing SD card is reported as `SD:FAIL`; NVS logs remain available
  independently.
- WIDS and tracker-name results are indicators for troubleshooting, not
  definitive security findings.

## Engineering dashboard

A generated engineering dashboard is available at [dashboard/output/DASHBOARD.md](dashboard/output/DASHBOARD.md).

To regenerate the dashboard locally:

```bash
python dashboard/dashboard.py
```

For automated updates, use the repository's CI workflow (see .github/workflows/dashboard.yml) to run the generator on push or schedule.
