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
static uint32_t now = 0, randomValue = 0; static int ntpCalls = 0; static bool storedSaved = false;
static bool acceptOrigin(const char *url) { return strcmp(url, "https://example.test") == 0; }
static void (*timeCallback)(struct timeval *) = nullptr;
void sntp_set_time_sync_notification_cb(void (*cb)(struct timeval *)) { timeCallback = cb; }
uint32_t millis() { return now; }
uint32_t esp_random() { return randomValue++; }
void configTime(long, int, const char *, const char *) { ++ntpCalls; }
int esp_wifi_get_config(int, wifi_config_t *cfg) { memset(cfg, 0, sizeof(*cfg)); if (storedSaved) cfg->sta.ssid[0] = 1; return ESP_OK; }
int main(int argc, char **) {
  storedSaved = argc > 1;
  if (storedSaved) { wifiServiceInit(); assert(wifiServiceState() == WifiServiceState::Connecting && WiFi.beginCount == 1); now = 20001; wifiServiceUpdate(); assert(wifiServiceState() == WifiServiceState::RetryWait); return 0; }
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
  WiFi.link = WL_CONNECTED; wifiServiceUpdate(); assert(ntpCalls == 1 && wifiServiceRssi() == -57);
  assert(!wifiServiceTimeReady() && timeCallback); timeCallback(nullptr); assert(wifiServiceTimeReady());
  now = 300001; wifiServiceUpdate();
  assert(!wifiServicePortalActive() && !WiFi.ap && wifiServiceState() == WifiServiceState::Connected);
  assert(strlen(wifiServicePortalPassword()) == 0);
  WiFi.link = WL_DISCONNECTED; wifiServiceUpdate(); assert(wifiServiceState() == WifiServiceState::RetryWait);
  now += 29999; wifiServiceUpdate(); assert(WiFi.beginCount == 1);
  now += 1; wifiServiceUpdate(); assert(WiFi.beginCount == 2 && wifiServiceState() == WifiServiceState::Connecting);
  WiFi.link = WL_CONNECTED; wifiServiceUpdate(); assert(wifiServiceState() == WifiServiceState::Connected);
}
