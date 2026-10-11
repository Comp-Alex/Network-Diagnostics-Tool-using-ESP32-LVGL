#include "SubnetScanner.h"

#include <WiFi.h>
#include <WiFiClient.h>

namespace {
uint32_t nextAddress = 0;
uint32_t finalAddress = 0;
uint32_t localAddress = 0;
uint32_t gatewayAddress = 0;
uint16_t testedHosts = 0;
uint32_t scanTotalHosts = 0;
String discovered;
bool scanRunning = false;
bool scanComplete = false;

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
bool start() {
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
  scanRunning = nextAddress <= finalAddress;
  return scanRunning;
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

  WiFiClient client;
  client.setTimeout(25);
  if (client.connect(addressFromValue(candidateValue), 80, 25)) {
    if (discovered.length() > 0) discovered += ", ";
    discovered += addressFromValue(candidateValue).toString();
    client.stop();
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

uint8_t progressPercent() {
  if (scanTotalHosts == 0) return scanComplete ? 100 : 0;
  const uint32_t progress = static_cast<uint32_t>(testedHosts) * 100 / scanTotalHosts;
  return static_cast<uint8_t>(progress > 100 ? 100 : progress);
}

String discoveredHosts() {
  if (discovered.length() == 0) return "No TCP port 80 responders found.";
  return discovered;
}
}  // namespace SubnetScanner
