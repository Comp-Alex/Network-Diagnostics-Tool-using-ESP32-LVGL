#include "SubnetScanner.h"

#include <WiFi.h>
#include <WiFiClient.h>

namespace {
constexpr uint16_t DeviceDiscoveryPorts[] = {80, 443, 22, 445, 8080};
uint32_t nextAddress = 0;
uint32_t finalAddress = 0;
uint32_t localAddress = 0;
uint32_t gatewayAddress = 0;
uint16_t testedHosts = 0;
uint32_t scanTotalHosts = 0;
String discovered;
bool scanRunning = false;
bool scanComplete = false;
bool scanForDevices = false;

uint32_t addressValue(const IPAddress &address) {
  return (static_cast<uint32_t>(address[0]) << 24) |
         (static_cast<uint32_t>(address[1]) << 16) |
         (static_cast<uint32_t>(address[2]) << 8) |
         static_cast<uint32_t>(address[3]);
}

IPAddress addressFromValue(uint32_t value) {
  return IPAddress((value >> 24) & 0xFF, (value >> 16) & 0xFF,
                   (value >> 8) & 0xFF, value & 0xFF);
}
}  // namespace

namespace SubnetScanner {
bool startScan(bool findDevices) {
  if (scanRunning) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  IPAddress ip = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  const uint32_t maskValue = addressValue(mask);
  localAddress = addressValue(ip);
  gatewayAddress = addressValue(WiFi.gatewayIP());
  const uint32_t networkAddress = localAddress & maskValue;
  const uint32_t broadcastAddress = networkAddress | ~maskValue;
  nextAddress = networkAddress + 1;
  finalAddress = broadcastAddress > 0 ? broadcastAddress - 1 : 0;
  testedHosts = 0;
  scanTotalHosts = broadcastAddress > networkAddress + 1
                       ? broadcastAddress - networkAddress - 1
                       : 0;
  discovered = "";
  scanComplete = false;
  scanForDevices = findDevices;
  scanRunning = nextAddress <= finalAddress;
  return scanRunning;
}

bool start() {
  return startScan(false);
}

bool startDeviceDiscovery() {
  return startScan(true);
}

void cancel() {
  scanRunning = false;
  scanComplete = false;
  scanForDevices = false;
}

void service() {
  if (!scanRunning) return;
  if (nextAddress > finalAddress) {
    scanRunning = false;
    scanComplete = true;
    return;
  }

  const uint32_t candidateValue = nextAddress++;
  testedHosts++;
  if (candidateValue == localAddress || candidateValue == gatewayAddress) return;

  const IPAddress candidate = addressFromValue(candidateValue);
  const size_t portCount = scanForDevices
                               ? sizeof(DeviceDiscoveryPorts) / sizeof(DeviceDiscoveryPorts[0])
                               : 1;
  for (size_t index = 0; index < portCount; index++) {
    const uint16_t port = scanForDevices ? DeviceDiscoveryPorts[index] : 80;
    WiFiClient client;
    client.setTimeout(25);
    if (!client.connect(candidate, port, 25)) continue;
    if (discovered.length() > 0) discovered += "\n";
    discovered += candidate.toString();
    if (scanForDevices) discovered += "  TCP " + String(port);
    client.stop();
    break;
  }

  if (nextAddress > finalAddress) {
    scanRunning = false;
    scanComplete = true;
  }
}

bool running() {
  return scanRunning;
}

bool complete() {
  return scanComplete;
}

bool deviceDiscovery() {
  return scanForDevices;
}

uint8_t progressPercent() {
  if (scanTotalHosts == 0) return scanComplete ? 100 : 0;
  const uint32_t progress = static_cast<uint32_t>(testedHosts) * 100 / scanTotalHosts;
  return static_cast<uint8_t>(progress > 100 ? 100 : progress);
}

String discoveredHosts() {
  if (discovered.length() == 0) {
    return scanForDevices ? "No responders on the probed TCP ports."
                          : "No TCP port 80 responders found.";
  }
  return discovered;
}
}  // namespace SubnetScanner
