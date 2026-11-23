#include <cassert>
#include <cstring>
#include <map>
#include "Arduino.h"
#include "WiFi.h"
#include "WebServer.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "wifi_service.h"
FakeWiFi WiFi;
std::map<std::string, void(*)()> WebServer::routes;
std::map<std::string, String> WebServer::args;
int WebServer::lastStatus = 0;
static uint32_t now = 0, randomValue = 0;
static int ntpCalls = 0, channelCalls = 0;
static bool failStorage = false, failChannel = false, wrongReadback = false;
static uint8_t channel = 1;
#if !RADAR_UI_TRIAL
static bool acceptOrigin(const char *url) { return strcmp(url, "https://example.test") == 0; }
#endif
static void (*timeCallback)(struct timeval *) = nullptr;
void sntp_set_time_sync_notification_cb(void (*cb)(struct timeval *)) { timeCallback = cb; }
uint32_t millis() { return now; }
uint32_t esp_random() { return randomValue++; }
void configTime(long, int, const char *, const char *) { ++ntpCalls; }
int esp_wifi_get_config(int, wifi_config_t *cfg) { *cfg = WiFi.flash; return ESP_OK; }
int esp_wifi_set_storage(wifi_storage_t storage) {
  if (failStorage) return -1;
  // Arduino startup must already have selected RAM, before starting STA.
  assert(WiFi.modeValue == WIFI_STA && WiFi.storage == storage && !WiFi.reconnect);
  WiFi.storage = storage;
  return ESP_OK;
}
int esp_wifi_get_mode(wifi_mode_t *mode) { *mode = wifi_mode_t(WiFi.modeValue); return ESP_OK; }
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *) { return WiFi.link == WL_CONNECTED ? ESP_OK : ESP_ERR_WIFI_NOT_CONNECT; }
int esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t) {
  ++channelCalls;
  if (failChannel) return -1;
  channel = primary;
  return ESP_OK;
}
int esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *) {
  *primary = wrongReadback ? 1 : channel;
  return ESP_OK;
}
int main(int argc, char **argv) {
#if RADAR_UI_TRIAL
  memcpy(WiFi.flash.sta.ssid, "saved-network", sizeof("saved-network"));
  memcpy(WiFi.flash.sta.password, "saved-password", sizeof("saved-password"));
  const auto original = WiFi.flash;
  failStorage = argc > 1 && strcmp(argv[1], "storage-fail") == 0;
  wifiServiceInit();
  if (failStorage) {
    assert(WiFi.modeValue == WIFI_OFF && !wifiServicePrepareOfflineChannel(6));
    assert(channelCalls == 0);
  } else {
    assert(WiFi.modeValue == WIFI_STA && WiFi.storage == WIFI_STORAGE_RAM);
    failChannel = true;
    assert(!wifiServicePrepareOfflineChannel(6) && channel == 1);
    failChannel = false;
    wrongReadback = true;
    assert(!wifiServicePrepareOfflineChannel(6));
    wrongReadback = false;
    assert(wifiServicePrepareOfflineChannel(11) && channel == 11);
    assert(wifiServicePrepareOfflineChannel(6) && channel == 6);
    WiFi.link = WL_CONNECTED;
    assert(!wifiServicePrepareOfflineChannel(1) && channel == 6 && channelCalls == 4);
    wifiServiceUpdate();
    WiFi.link = WL_DISCONNECTED;
  }
  wifiServiceBeginPortal();
  now = 600000;
  wifiServiceUpdate();
  assert(!WiFi.ap && !wifiServicePortalActive() && WebServer::routes.empty());
  assert(!WiFi.persistence && !WiFi.reconnect && WiFi.beginCount == 0 && ntpCalls == 0);
  assert(wifiServiceState() == WifiServiceState::Offline);
  assert(memcmp(&original, &WiFi.flash, sizeof(original)) == 0);
#else
  (void)argv;
  if (argc > 1) { WiFi.flash.sta.ssid[0] = 1; wifiServiceInit(); assert(wifiServiceState() == WifiServiceState::Connecting && WiFi.beginCount == 1); now = 20001; wifiServiceUpdate(); assert(wifiServiceState() == WifiServiceState::RetryWait); return 0; }
  wifiServiceInit(); wifiServiceUpdate();
  assert(wifiServiceState() == WifiServiceState::Offline);
  wifiServiceSetServerOriginHandler(acceptOrigin);
  wifiServiceBeginPortal();
  assert(wifiServicePortalActive() && WiFi.ap && strlen(wifiServicePortalPassword()) == 12);
  WebServer::args["url"] = "http://example.test"; WebServer::routes["/server"](); assert(WebServer::lastStatus == 400);
  WebServer::args["url"] = "https://example.test"; WebServer::routes["/server"](); assert(WebServer::lastStatus == 200);
  WebServer::args["s"] = "test-network"; WebServer::args["p"] = "short";
  WebServer::routes["/save"](); assert(WebServer::lastStatus == 400 && WiFi.beginCount == 0);
  WebServer::args["p"] = "long-enough";
  WebServer::routes["/save"](); assert(WebServer::lastStatus == 200 && WiFi.beginCount == 1);
  assert(WiFi.storage == WIFI_STORAGE_FLASH);
  assert(strcmp(reinterpret_cast<const char *>(WiFi.flash.sta.ssid), "test-network") == 0);
  assert(strcmp(reinterpret_cast<const char *>(WiFi.flash.sta.password), "long-enough") == 0);
  WiFi.link = WL_CONNECTED; wifiServiceUpdate(); assert(ntpCalls == 1 && wifiServiceRssi() == -57);
  assert(!wifiServiceTimeReady() && timeCallback); timeCallback(nullptr); assert(wifiServiceTimeReady());
  now = 300001; wifiServiceUpdate();
  assert(!wifiServicePortalActive() && !WiFi.ap && wifiServiceState() == WifiServiceState::Connected);
  assert(strlen(wifiServicePortalPassword()) == 0);
  WiFi.link = WL_DISCONNECTED; wifiServiceUpdate(); assert(wifiServiceState() == WifiServiceState::RetryWait);
  now += 29999; wifiServiceUpdate(); assert(WiFi.beginCount == 1);
  now += 1; wifiServiceUpdate(); assert(WiFi.beginCount == 2 && wifiServiceState() == WifiServiceState::Connecting);
  WiFi.link = WL_CONNECTED; wifiServiceUpdate(); assert(wifiServiceState() == WifiServiceState::Connected);
#endif
}
