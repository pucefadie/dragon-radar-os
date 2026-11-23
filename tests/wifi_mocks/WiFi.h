#pragma once
#include "Arduino.h"
#include "esp_wifi.h"
enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP_STA = 3, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
struct FakeWiFi {
  int link = WL_DISCONNECTED, beginCount = 0, modeValue = 0;
  bool ap = false, persistence = true, reconnect = true;
  wifi_storage_t storage = WIFI_STORAGE_FLASH;
  wifi_config_t flash{};
  bool mode(int m) {
    if (modeValue == WIFI_OFF && m != WIFI_OFF)
      storage = persistence ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM;
    modeValue = m;
    return true;
  }
  void setAutoReconnect(bool value) { reconnect = value; }
  void persistent(bool value) { persistence = value; }
  void begin() { ++beginCount; }
  void begin(const char *ssid, const char *password) {
    ++beginCount;
    if (storage == WIFI_STORAGE_FLASH) {
      strncpy(reinterpret_cast<char *>(flash.sta.ssid), ssid, sizeof(flash.sta.ssid));
      strncpy(reinterpret_cast<char *>(flash.sta.password), password, sizeof(flash.sta.password));
    }
  }
  bool softAP(const char *, const char *key) { ap = strlen(key) >= 8; return ap; }
  int softAPIP() { return 1; }
  void softAPdisconnect(bool) { ap = false; }
  int status() const { return link; }
  int RSSI() const { return -57; }
  void disconnect(bool, bool) { link = WL_DISCONNECTED; }
};
extern FakeWiFi WiFi;
