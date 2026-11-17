#pragma once
#include "Arduino.h"
enum { WIFI_STA = 1, WIFI_AP_STA = 3, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
struct FakeWiFi {
  int link = WL_DISCONNECTED, beginCount = 0, modeValue = 0; bool ap = false;
  void mode(int m) { modeValue = m; }
  void setAutoReconnect(bool) {}
  void persistent(bool) {}
  void begin() { ++beginCount; }
  void begin(const char *, const char *) { ++beginCount; }
  bool softAP(const char *, const char *key) { ap = strlen(key) >= 8; return ap; }
  int softAPIP() { return 1; }
  void softAPdisconnect(bool) { ap = false; }
  int status() const { return link; }
  int RSSI() const { return -57; }
  void disconnect(bool, bool) { link = WL_DISCONNECTED; }
};
extern FakeWiFi WiFi;
