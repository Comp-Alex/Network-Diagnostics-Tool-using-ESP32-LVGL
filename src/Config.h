#pragma once

#include <Arduino.h>

namespace NdtConfig {
constexpr uint16_t ScreenWidth = 320;
constexpr uint16_t ScreenHeight = 240;

constexpr uint8_t TftBacklightPin = 21;
constexpr uint8_t TouchSckPin = 25;
constexpr uint8_t TouchMisoPin = 39;
constexpr uint8_t TouchMosiPin = 32;
constexpr uint8_t TouchCsPin = 33;
constexpr uint8_t TouchIrqPin = 36;

constexpr uint8_t RtcSclPin = 22;
constexpr uint8_t RtcSdaPin = 27;
constexpr uint8_t SdCsPin = 5;
constexpr uint8_t SdSckPin = 18;
constexpr uint8_t SdMisoPin = 19;
constexpr uint8_t SdMosiPin = 23;
constexpr uint8_t AlertSpeakerPin = 26;

constexpr uint8_t MaxNetworks = 12;
constexpr uint8_t MaxBleDevices = 12;
constexpr uint8_t MaxLogEntries = 8;
constexpr uint32_t BleScanSeconds = 5;
constexpr uint32_t WiFiConnectionTimeoutMs = 10000;
constexpr uint32_t WiFiScanTimeoutMs = 20000;
constexpr uint32_t WiFiScanDwellMs = 800;
constexpr uint32_t StatusRefreshMs = 1000;
constexpr uint32_t SplashDurationMs = 8000;
constexpr uint32_t WidsDeauthWindowMs = 10000;
constexpr uint8_t WidsDeauthThreshold = 10;
constexpr uint32_t NtpSyncRetryMs = 60000;
constexpr char TimeZone[] = "SGT-8";
constexpr char NtpServerOne[] = "pool.ntp.org";
constexpr char NtpServerTwo[] = "time.google.com";
constexpr char NtpServerThree[] = "time.cloudflare.com";
}  // namespace NdtConfig
