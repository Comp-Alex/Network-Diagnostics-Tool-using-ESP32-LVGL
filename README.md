# Wi-Fi Scope for ESP32-2432S028R

Touch-controlled Wi-Fi diagnostics for the 2.8-inch ESP32-2432S028R (CYD) board. The firmware uses Arduino, PlatformIO, LVGL, TFT_eSPI, and XPT2046 to provide:

- Nearby SSID, RSSI, and channel scanning.
- Tap a visible Wi-Fi network, enter its password on the touchscreen, and connect.
- Nearby Bluetooth Low Energy (BLE) advertiser scanning with signal strength and address.
- Toggle-controlled secured Wi-Fi access-point mode and BLE beacon.
- A channel occupancy bar chart for channels 1-13.
- Five HTTP connection probes to `1.1.1.1` with average latency and packet loss.
- An animated pixel-art startup screen with a wink and smile, shown for at least eight seconds.
- A persistent dark/light theme switch beside the NDT heading; dark mode starts enabled with a black and green palette.
- Touch tabs for Scan, BLE, Extend, Test, and Logs.
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
2. **UI:** LVGL provides the Wi-Fi Scan, BLE, Extend, Test, and Logs tabs, plus an on-screen keyboard.
3. **Diagnostics:** `WiFi.scanNetworks()` collects nearby network data, the ESP32 BLE stack scans BLE advertisements, and TCP connections to `1.1.1.1:80` estimate reachability and latency.
4. **Storage:** Preferences/NVS stores the latest eight log entries across restarts.

## Hardware

- ESP32-2432S028R board with ILI9341 TFT and XPT2046 touch controller.
- USB cable.
- Wi-Fi access point with internet access for the latency test.

The project is configured for the common 2432S028R wiring. The TFT uses SPI pins 12/13/14 on HSPI, while the touch controller has its own SPI pin mapping on VSPI (SCK 25, MISO 39, MOSI 32, CS 33, IRQ 36). TFT CS is 15, TFT DC is 2, and the backlight is 21. Board revisions can vary; if touch coordinates are mirrored or offset, adjust the calibration bounds in `src/main.cpp`.

## Build and upload

1. Install VS Code with PlatformIO.
2. Open this folder as a PlatformIO project.
3. Run `pio run -t upload` and open the serial monitor at 115200 baud. Uploads use
   115200 baud for reliability.

The BLE support increases firmware size, so this project uses the ESP32 `huge_app.csv` partition layout. This provides a larger application partition on 4 MB boards, with less room allocated to SPIFFS.

The display starts in landscape orientation with an animated NDT startup screen, including the “Network Diagnostic tool” subtitle and author credit. The pixel-art eyes wink and the face smiles; after at least eight seconds, the dashboard opens. Use the unlabeled switch beside **NDT** to change themes. Dark mode is the default and uses a black background with green accents; the chosen theme is saved across restarts.

Tap **SCAN WI-FI**, then select a network name to open the on-screen password keyboard. Each result separates the network name from its right-aligned signal strength in dBm. Tap the keyboard's **OK** key to connect; leave the password blank for an open network. Passwords are used for the current session only and are not written to NVS or diagnostic logs. Tap **SCAN BLUETOOTH LE** on the BLE tab to discover nearby BLE advertisers and view their RSSI and address. On **EXTEND**, use the switches to start or stop a protected Wi-Fi access point after connecting to an upstream network, or broadcast the NDT BLE beacon. **RUN DIAGNOSTICS** performs five connection attempts and records the result in the Logs tab.

## Board pin map

| Function | GPIO |
| --- | ---: |
| TFT SCK | 14 |
| TFT MISO | 12 |
| TFT MOSI | 13 |
| TFT chip select | 15 |
| TFT data/command | 2 |
| TFT backlight | 21 |
| TFT reset | Not connected (`-1`) |
| Touch SCK | 25 |
| Touch MISO | 39 |
| Touch MOSI | 32 |
| Touch chip select | 33 |
| Touch interrupt | 36 |

The TFT and touch controller use separate SPI buses on this board. TFT_eSPI is assigned HSPI, and the XPT2046 library's global VSPI bus is initialized with the touch controller's pins.

## Wi-Fi credentials

Choose a nearby network from the Wi-Fi Scan tab and enter its password using the on-screen keyboard. Credentials are not stored in flash and must be entered again after a reboot. The ESP32 supports 2.4 GHz Wi-Fi networks.

## Using the dashboard

### Scan tab

Press **SCAN WI-FI** to refresh the nearby access point list. Each row shows the SSID in a selectable name box and the signal strength in dBm in a separate box on the right. Tap a network name to enter its password and connect. The orange chart shows how many visible networks were found on each channel from 1 through 13.

### BLE tab

Press **SCAN BLUETOOTH LE** to scan for nearby BLE advertisements. Results show the advertised name (or indicate that the device is unnamed), RSSI, and Bluetooth address. This is a passive BLE discovery and signal-strength check; it does not pair with devices or scan Bluetooth Classic devices.

### Extend tab

Connect to an upstream Wi-Fi network first, then turn on the **Hotspot** switch for concurrent station and access-point operation as **NDT-EXTENDER**. The hotspot password is generated from the board's Wi-Fi MAC address and shown on screen; up to four clients can join. Turn the switch off to stop the hotspot. The bundled ESP32 Arduino SDK has IPv4 forwarding/NAPT disabled, so connected clients do not receive internet through this hotspot.

Turn on the **BLE beacon** switch to scan for nearby BLE advertisers, then broadcast an `NDT-BLE-RELAY` beacon. Turn it off to stop advertising. It does not retransmit another device's advertisements, connections, or data.

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
| Display is blank | Confirm the TFT_eSPI pin definitions in `platformio.ini`, confirm the backlight is wired to GPIO 21 on this board revision, and use a USB cable that supplies power. |
| Touch does not respond | Verify touch CS `33`, IRQ `36`, and the calibration range. |
| No networks appear | Confirm Wi-Fi credentials and wait for the ESP32 to associate before scanning. |
| Status says connection failed | Confirm the SSID/password, use a 2.4 GHz network, and allow the automatic retry to run. |
| Diagnostics show 100% loss | Check internet access; the test endpoint must be reachable on TCP port 80. |
| Build cannot find headers | Run the project through PlatformIO so its dependencies are installed from `platformio.ini`. |
| Upload reports `Wrong boot mode detected` | Start the upload, then hold **BOOT** while tapping **EN/RESET**. Keep holding **BOOT** until esptool starts writing, then release it. Use a USB data cable, select the ESP32's USB serial port, and close the serial monitor before uploading. |

## Notes

- The firmware intentionally limits the visible scan list to 12 networks so the touch UI remains responsive.
- NVS logging survives reset and power cycles, but it is a small rolling history. Add an SD library and writer if full historical export is required.
- The channel chart measures the number of detected access points, not actual radio airtime utilization.
