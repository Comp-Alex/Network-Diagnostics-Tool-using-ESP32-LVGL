#pragma once

#include <Arduino.h>

namespace WidsGuard {
struct Alert {
  char message[80];
};

void begin();
bool start();
void stop();
void service();
bool popAlert(Alert &alert);
uint8_t currentChannel();
}  // namespace WidsGuard
