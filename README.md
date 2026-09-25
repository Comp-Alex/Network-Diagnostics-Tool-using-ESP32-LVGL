# Wi-Fi Scope for ESP32-2432S028R

Touch-controlled Wi-Fi diagnostics for the 2.8-inch ESP32-2432S028R (CYD) board. The firmware uses Arduino, PlatformIO, LVGL, TFT_eSPI, and XPT2046 to provide:

- Nearby SSID, RSSI, and channel scanning.
- A channel occupancy bar chart for channels 1-13.
- Five HTTP connection probes to `1.1.1.1` with average latency and packet loss.
- Touch tabs for Scan, Test, and Logs.
- Persistent diagnostic history in ESP32 NVS, so an SD card is optional.

## Project structure

```text
.
├── platformio.ini       PlatformIO board and library configuration
├── src/main.cpp         LVGL UI, Wi-Fi tests, touch input, and NVS logging
└── README.md            Build, wiring, and usage documentation
```

The firmware is intentionally kept in one source file so it can be opened and modified easily in Arduino or PlatformIO projects. The main runtime layers are:

1. **Hardware:** TFT_eSPI renders the ILI9341 display and XPT2046 reads touch input.
2. **UI:** LVGL provides the Scan, Test, and Logs tabs.
3. **Diagnostics:** `WiFi.scanNetworks()` collects nearby network data and TCP connections to `1.1.1.1:80` estimate reachability and latency.
4. **Storage:** Preferences/NVS stores the latest eight log entries across restarts.

## Hardware

- ESP32-2432S028R board with ILI9341 TFT and XPT2046 touch controller.
- USB cable.
- Wi-Fi access point with internet access for the latency test.

The project is configured for the common 2432S028R wiring: TFT SPI pins 12/13/14, TFT CS 15, TFT DC 2, touch CS 33, and touch IRQ 36. Board revisions can vary; if touch coordinates are mirrored or offset, adjust the calibration bounds in `src/main.cpp`.

## Build and upload

1. Install VS Code with PlatformIO.
2. Open this folder as a PlatformIO project.
3. Run `pio run -t upload` and open the serial monitor at 115200 baud.
4. Configure Wi-Fi credentials using the board's stored Arduino Wi-Fi credentials, or replace `WiFi.begin()` in `src/main.cpp` with `WiFi.begin("SSID", "PASSWORD")` before uploading.

The display starts in landscape orientation. Tap **SCAN NETWORKS** after Wi-Fi connects. **RUN DIAGNOSTICS** performs five connection attempts and records the result in the Logs tab.

## Board pin map

| Function | GPIO |
| --- | ---: |
| TFT SCK | 14 |
| TFT MISO | 12 |
| TFT MOSI | 13 |
| TFT chip select | 15 |
| TFT data/command | 2 |
| TFT reset | Not connected (`-1`) |
| Touch chip select | 33 |
| Touch interrupt | 36 |

The display and touch controller share the SPI bus. The firmware initializes the bus explicitly before starting either device.

## Wi-Fi credentials

The default `WiFi.begin()` call allows the ESP32 Arduino Wi-Fi stack to use credentials already stored on the device. For a first-time setup, replace it with credentials before uploading:

```cpp
WiFi.begin("network-name", "network-password");
```

Avoid committing real credentials to source control. A local PlatformIO build flag or a private `include/secrets.h` file is safer for repeated development builds.

## Using the dashboard

### Scan tab

Press **SCAN NETWORKS** to refresh the nearby access point list. Each row shows the SSID, RSSI in dBm, and channel. The orange chart shows how many visible networks were found on each channel from 1 through 13.

### Test tab

Press **RUN DIAGNOSTICS** while connected to Wi-Fi. The firmware makes five TCP connection attempts to Cloudflare's `1.1.1.1` endpoint. The result shows average successful connection time and packet loss percentage. This is an internet reachability test, not a full ICMP ping or throughput benchmark.

### Logs tab

Scan and diagnostic results are saved to ESP32 non-volatile storage. The UI keeps the newest eight entries. Flash storage is not an unlimited event database, so use an SD card or external service if long-term history is required.

## Touch calibration

The default calibration range is defined in `readTouch()` in `src/main.cpp`:

```cpp
map(point.x, 250, 3850, 0, ScreenWidth);
map(point.y, 200, 3850, 0, ScreenHeight);
```

If touch is offset, change the four raw calibration values. If X and Y are swapped or reversed, adjust the mapping axes or the `touch.setRotation(1)` setting. Calibration values differ between 2432S028R board revisions.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Display is blank | Confirm the TFT_eSPI pin definitions in `platformio.ini` and use a USB cable that supplies power. |
| Touch does not respond | Verify touch CS `33`, IRQ `36`, and the calibration range. |
| No networks appear | Confirm Wi-Fi credentials and wait for the ESP32 to associate before scanning. |
| Diagnostics show 100% loss | Check internet access; the test endpoint must be reachable on TCP port 80. |
| Build cannot find headers | Run the project through PlatformIO so its dependencies are installed from `platformio.ini`. |

## Notes

- The firmware intentionally limits the visible scan list to 12 networks so the touch UI remains responsive.
- Channel chart values count visible access points, not airtime utilization.
- NVS logging survives reset and power cycles, but it is a small rolling history. Add an SD library and writer if full historical export is required.
- The channel chart measures the number of detected access points, not actual radio airtime utilization.
