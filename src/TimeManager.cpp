#include "TimeManager.h"

#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>

#include "Config.h"

namespace {
constexpr uint8_t Ds3231Address = 0x68;
constexpr time_t ValidEpoch = 1704067200;

bool rtcPresent = false;
bool ntpSynced = false;
bool ntpStarted = false;
bool previousWifiConnected = false;
uint32_t lastNtpAttempt = 0;

uint8_t fromBcd(uint8_t value) {
  return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0F));
}

uint8_t toBcd(uint8_t value) {
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool readRtc(struct tm &value) {
  Wire.beginTransmission(Ds3231Address);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(Ds3231Address, static_cast<uint8_t>(7)) != 7) return false;

  uint8_t second = fromBcd(Wire.read() & 0x7F);
  uint8_t minute = fromBcd(Wire.read() & 0x7F);
  uint8_t hourRaw = Wire.read();
  uint8_t hour = (hourRaw & 0x40)
                     ? static_cast<uint8_t>((fromBcd(hourRaw & 0x1F) % 12) + ((hourRaw & 0x20) ? 12 : 0))
                     : fromBcd(hourRaw & 0x3F);
  Wire.read();
  uint8_t day = fromBcd(Wire.read() & 0x3F);
  uint8_t monthRaw = Wire.read();
  uint8_t month = fromBcd(monthRaw & 0x1F);
  uint16_t year = static_cast<uint16_t>(2000 + fromBcd(Wire.read()));
  if (monthRaw & 0x80) year += 100;

  if (second > 59 || minute > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12) {
    return false;
  }

  value = {};
  value.tm_sec = second;
  value.tm_min = minute;
  value.tm_hour = hour;
  value.tm_mday = day;
  value.tm_mon = month - 1;
  value.tm_year = year - 1900;
  value.tm_isdst = -1;
  return true;
}

void writeRtc(const struct tm &value) {
  Wire.beginTransmission(Ds3231Address);
  Wire.write(0x00);
  Wire.write(toBcd(static_cast<uint8_t>(value.tm_sec)));
  Wire.write(toBcd(static_cast<uint8_t>(value.tm_min)));
  Wire.write(toBcd(static_cast<uint8_t>(value.tm_hour)));
  Wire.write(toBcd(static_cast<uint8_t>(value.tm_wday == 0 ? 7 : value.tm_wday)));
  Wire.write(toBcd(static_cast<uint8_t>(value.tm_mday)));
  Wire.write(toBcd(static_cast<uint8_t>(value.tm_mon + 1)));
  Wire.write(toBcd(static_cast<uint8_t>((value.tm_year + 1900) % 100)));
  Wire.endTransmission();
}

bool getCurrentTime(struct tm &value) {
  time_t now = time(nullptr);
  if (now < ValidEpoch || !localtime_r(&now, &value)) return false;
  return true;
}

void seedSystemClockFromRtc() {
  struct tm rtcTime {};
  if (!readRtc(rtcTime)) return;
  time_t epoch = mktime(&rtcTime);
  if (epoch < ValidEpoch) return;
  struct timeval clockValue {};
  clockValue.tv_sec = epoch;
  settimeofday(&clockValue, nullptr);
}
}  // namespace

namespace TimeManager {
void begin() {
  setenv("TZ", NdtConfig::TimeZone, 1);
  tzset();
  Wire.begin(NdtConfig::RtcSdaPin, NdtConfig::RtcSclPin);
  Wire.setClock(100000);
  Wire.beginTransmission(Ds3231Address);
  rtcPresent = Wire.endTransmission() == 0;
  if (rtcPresent) seedSystemClockFromRtc();
}

void service(bool wifiConnected) {
  struct tm current {};
  const bool hasTime = getCurrentTime(current);

  if (wifiConnected && !previousWifiConnected) {
    configTzTime(NdtConfig::TimeZone, NdtConfig::NtpServerOne,
                 NdtConfig::NtpServerTwo, NdtConfig::NtpServerThree);
    lastNtpAttempt = millis();
    ntpStarted = true;
    ntpSynced = false;
  }
  previousWifiConnected = wifiConnected;

  if (ntpStarted && !ntpSynced &&
      sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && hasTime) {
    ntpSynced = true;
    if (rtcPresent) writeRtc(current);
  }

  if (!wifiConnected || ntpSynced ||
      (lastNtpAttempt != 0 &&
       static_cast<uint32_t>(millis() - lastNtpAttempt) < NdtConfig::NtpSyncRetryMs)) {
    return;
  }

  configTzTime(NdtConfig::TimeZone, NdtConfig::NtpServerOne,
               NdtConfig::NtpServerTwo, NdtConfig::NtpServerThree);
  lastNtpAttempt = millis();
  ntpStarted = true;
}

bool timeIsValid() {
  return time(nullptr) >= ValidEpoch;
}

bool rtcIsPresent() {
  return rtcPresent;
}

bool ntpIsSynchronized() {
  return ntpSynced;
}

String dateString() {
  struct tm value {};
  if (!getCurrentTime(value)) return "0000-00-00";
  char output[11];
  strftime(output, sizeof(output), "%Y-%m-%d", &value);
  return String(output);
}

String timeString() {
  struct tm value {};
  if (!getCurrentTime(value)) return "--:--:--";
  char output[9];
  strftime(output, sizeof(output), "%H:%M:%S", &value);
  return String(output);
}

String timestamp() {
  if (!timeIsValid()) return String("uptime-") + String(millis() / 1000);
  struct tm value {};
  if (!getCurrentTime(value)) return String("uptime-") + String(millis() / 1000);
  char output[20];
  strftime(output, sizeof(output), "%Y-%m-%d %H:%M:%S", &value);
  return String(output);
}
}  // namespace TimeManager
