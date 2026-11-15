#pragma once
#include <Arduino.h>
#define WL_CONNECTED 3
#define WIFI_STA 1
struct IPAddress { String toString() const { return String("192.168.0.77"); } };
struct WiFiClass {
  void mode(int) {} void setSleep(bool) {} void setAutoReconnect(bool) {}
  void begin(const char*, const char*) {} void disconnect() {}
  int status() { return WL_CONNECTED; } IPAddress localIP() { return IPAddress(); } int RSSI() { return -52; }
};
extern WiFiClass WiFi;
