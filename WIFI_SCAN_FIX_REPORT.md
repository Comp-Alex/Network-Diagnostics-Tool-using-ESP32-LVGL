# Wi-Fi Scan Fix Report

## Summary

The Connections page starts an asynchronous Wi-Fi scan and checks its status
from the main loop. Failed scans previously produced only a generic status,
and a scan that remained in the running state had no timeout or recovery path.
That could leave scanning unavailable until the user navigated away or rebooted.

## Changes

- Clear stale scan results before starting a new scan.
- Pass the scan options explicitly while keeping the scan asynchronous.
- Increase the per-channel dwell from 300 ms to 800 ms, giving the radio
  more time to hear beacons/probe responses on each 2.4 GHz channel. The scan
  timeout is 20 seconds to accommodate the longer sweep.
- Keep the “scanning” indicator visible while the periodic status refresh runs.
- Stop and clean up a scan that exceeds the 20-second limit.
- Record driver result codes in the saved log and show them in the status text
  when startup or completion fails.
- Document the timeout and recovery behavior in the README.

## Validation and deployment

- The previous version built and was flashed successfully, but the user
  reported that nearby 2.4 GHz networks were still not detected.
- Updated `pio run` — passed for `esp32dev`.
- Updated `pio run -t upload --upload-port COM11` — passed; flash hash was
  verified and the board reset after upload.
- User confirmed that nearby Wi-Fi networks appear after the update.
