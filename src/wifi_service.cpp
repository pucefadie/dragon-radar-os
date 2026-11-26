#include "wifi_service.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <atomic>
#ifndef RADAR_UI_TRIAL
#define RADAR_UI_TRIAL 0
#endif
namespace {
WebServer server(80);
DNSServer dns;
bool active = false, initialized = false, savedCredentials = false, timeConfigured = false, handlersRegistered = false;
bool (*originHandler)(const char *) = nullptr;
uint32_t started = 0, retryAt = 0, connectStarted = 0;
char portalPassword[13] = {0};
WifiServiceState state = WifiServiceState::Offline;
std::atomic<bool> timeSynchronized{false};
void onTimeSync(struct timeval *) { timeSynchronized.store(true); }
constexpr uint32_t portalLifetime = 300000UL, connectionTimeout = 20000UL, retryInterval = 30000UL;
void form() {
  server.send(200,"text/html", "<!doctype html><meta name='viewport' content='width=device-width'><title>Dragon Radar Wi-Fi</title><h2>Dragon Radar Wi-Fi</h2><form method='POST' action='/save'><label>SSID <input name='s' maxlength='32' required></label><br><label>Password <input type='password' name='p' maxlength='63'></label><br><button>Connetti</button></form><hr><form method='POST' action='/server'><label>Server HTTPS <input type='url' name='url' maxlength='127' required></label><button>Salva indirizzo</button></form><p>Non inserire password o token Audiobookshelf qui.</p>");
}
void save() {
  String ssid = server.arg("s"), password = server.arg("p");
  if (!ssid.length() || ssid.length() > 32 || password.length() > 63 || (password.length() > 0 && password.length() < 8)) {
    server.send(400,"text/plain","Dati non validi"); return;
  }
  server.send(200,"text/plain","Connessione avviata. Controlla lo stato sul radar.");
  WiFi.persistent(true);
  WiFi.begin(ssid.c_str(), password.c_str());
  savedCredentials = true; connectStarted = millis();
}
void saveServer() {
  String url = server.arg("url");
  if (!originHandler || !originHandler(url.c_str())) { server.send(400,"text/plain","Indirizzo HTTPS non valido o servizio occupato."); return; }
  server.send(200,"text/plain","Indirizzo salvato. Nessun login e stato effettuato.");
}
void stopPortal() {
  dns.stop(); server.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
  memset(portalPassword, 0, sizeof(portalPassword)); active = false;
}
}
void wifiServiceInit() {
  if (initialized) return;
  if (RADAR_UI_TRIAL) {
    // Arduino selects RAM storage inside low-level init when persistence is
    // disabled before entering STA mode. Check storage again before use.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    initialized = WiFi.mode(WIFI_STA) && esp_wifi_set_storage(WIFI_STORAGE_RAM) == ESP_OK;
    if (!initialized) WiFi.mode(WIFI_OFF);
    return;
  }
  initialized = true;
  WiFi.mode(WIFI_STA);
  sntp_set_time_sync_notification_cb(onTimeSync);
  WiFi.setAutoReconnect(false);
  // Load the station configuration saved by Arduino-ESP32 without printing it.
  wifi_config_t stored = {};
  savedCredentials = esp_wifi_get_config(WIFI_IF_STA, &stored) == ESP_OK && stored.sta.ssid[0] != 0;
  memset(&stored, 0, sizeof(stored));
  if (savedCredentials) { WiFi.begin(); connectStarted = millis(); state = WifiServiceState::Connecting; }
}
#if RADAR_UI_TRIAL
bool wifiServicePrepareOfflineChannel(uint8_t channel) {
  wifi_mode_t mode = WIFI_MODE_NULL;
  wifi_ap_record_t ap{};
  uint8_t primary = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  return initialized && esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_STA &&
         esp_wifi_sta_get_ap_info(&ap) == ESP_ERR_WIFI_NOT_CONNECT &&
         esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK &&
         esp_wifi_get_channel(&primary, &secondary) == ESP_OK && primary == channel;
}
#endif
void wifiServiceSetServerOriginHandler(bool (*handler)(const char *)) { originHandler = handler; }
void wifiServiceBeginPortal() {
  if (RADAR_UI_TRIAL || active) return;
  wifiServiceInit();
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  for (unsigned i = 0; i < sizeof(portalPassword)-1; ++i) portalPassword[i] = alphabet[esp_random() & 31];
  portalPassword[sizeof(portalPassword)-1] = '\0';
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP("DragonRadar-Setup", portalPassword)) { memset(portalPassword, 0, sizeof(portalPassword)); return; }
  dns.start(53,"*",WiFi.softAPIP());
  if (!handlersRegistered) { server.on("/",HTTP_GET,form); server.on("/save",HTTP_POST,save); server.on("/server",HTTP_POST,saveServer); server.onNotFound(form); handlersRegistered = true; }
  server.begin();
  active = true; started = millis(); state = WifiServiceState::Provisioning;
}
void wifiServiceUpdate() {
  if (RADAR_UI_TRIAL || !initialized) return;
  const uint32_t now = millis();
  if (active) {
    dns.processNextRequest(); server.handleClient();
    if ((uint32_t)(now - started) >= portalLifetime) stopPortal();
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!timeConfigured) { configTime(0, 0, "pool.ntp.org", "time.cloudflare.com"); timeConfigured = true; }
    state = active ? WifiServiceState::Provisioning : WifiServiceState::Connected;
  } else if (active) { timeConfigured = false; state = WifiServiceState::Provisioning; }
  else if (!savedCredentials) { timeConfigured = false; state = WifiServiceState::Offline; }
  else if (state == WifiServiceState::Connected || state == WifiServiceState::Provisioning || state == WifiServiceState::Offline) {
    timeConfigured = false; state = WifiServiceState::RetryWait; retryAt = now + retryInterval;
  } else if (state == WifiServiceState::Connecting && (uint32_t)(now - connectStarted) >= connectionTimeout) {
    state = WifiServiceState::RetryWait; retryAt = now + retryInterval;
  } else if (state == WifiServiceState::RetryWait && (int32_t)(now - retryAt) >= 0) {
    WiFi.disconnect(false, false); WiFi.begin(); connectStarted = now; state = WifiServiceState::Connecting;
  }
}
bool wifiServicePortalActive() { return active; }
const char *wifiServicePortalPassword() { return active ? portalPassword : ""; }
WifiServiceState wifiServiceState() { return state; }
const char *wifiServiceStatusText() {
  if (RADAR_UI_TRIAL) return "BANCO OFFLINE";
  switch (state) {
    case WifiServiceState::Connecting: return "CONNESSIONE";
    case WifiServiceState::Connected: return "CONNESSO";
    case WifiServiceState::Provisioning: return "SETUP";
    case WifiServiceState::RetryWait: return "RIPROVO";
    default: return "OFFLINE";
  }
}
int32_t wifiServiceRssi() { return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0; }

bool wifiServiceTimeReady() { return timeSynchronized.load(); }
