#include "StorageManager.h"

#include <SD.h>
#include <SPI.h>

#include "Config.h"

namespace {
constexpr char WidsLogPath[] = "/wids_alerts.csv";
bool cardAvailable = false;

void restoreTouchBus() {
  SD.end();
  SPI.end();
  SPI.begin(NdtConfig::TouchSckPin, NdtConfig::TouchMisoPin,
            NdtConfig::TouchMosiPin, NdtConfig::TouchCsPin);
}

bool mountCardBus() {
  SPI.end();
  SPI.begin(NdtConfig::SdSckPin, NdtConfig::SdMisoPin,
            NdtConfig::SdMosiPin, NdtConfig::SdCsPin);
  cardAvailable = SD.begin(NdtConfig::SdCsPin, SPI, 4000000);
  if (!cardAvailable) {
    SPI.end();
    SPI.begin(NdtConfig::TouchSckPin, NdtConfig::TouchMisoPin,
              NdtConfig::TouchMosiPin, NdtConfig::TouchCsPin);
  }
  return cardAvailable;
}

String csvEscape(const String &value) {
  String escaped = "\"";
  for (size_t index = 0; index < value.length(); index++) {
    if (value[index] == '"') escaped += '"';
    escaped += value[index];
  }
  escaped += '"';
  return escaped;
}
}  // namespace

namespace StorageManager {
bool begin() {
  if (!mountCardBus()) return false;
  if (!SD.exists(WidsLogPath)) {
    File file = SD.open(WidsLogPath, FILE_WRITE);
    if (!file) {
      cardAvailable = false;
      restoreTouchBus();
      return false;
    }
    file.println("timestamp,event");
    file.close();
  }
  cardAvailable = true;
  restoreTouchBus();
  return true;
}

bool available() {
  return cardAvailable;
}

void logWidsAlert(const String &timestamp, const String &event) {
  if (!mountCardBus()) return;
  File file = SD.open(WidsLogPath, FILE_APPEND);
  if (file) {
    file.print(csvEscape(timestamp));
    file.print(',');
    file.println(csvEscape(event));
    file.close();
    cardAvailable = true;
  } else {
    cardAvailable = false;
  }
  restoreTouchBus();
}

const char *statusText() {
  return cardAvailable ? "SD:OK" : "SD:FAIL";
}
}  // namespace StorageManager
