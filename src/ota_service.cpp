#include "ota_service.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <atomic>

namespace {
std::atomic<bool> active{false}, updating{false}, stopRequested{false};
std::atomic<int> progress{0}, result{0};
char password[4] = {};
uint32_t started = 0;
WebServer http(8266);
bool httpAuthorized = false, httpOk = false, httpConfigured = false;
void httpUpload() {
  HTTPUpload &upload = http.upload();
  if (upload.status == UPLOAD_FILE_START) {
    httpAuthorized = http.header("X-OTA-Key") == password;
    httpOk = false;
    if (!httpAuthorized) { result = 1; stopRequested = true; return; }
    const String digest = http.header("X-OTA-MD5");
    if (digest.length() != 32 || !Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { result = 1; return; }
    if (!Update.setMD5(digest.c_str())) { Update.abort(); result = 1; return; }
    updating = true;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (updating && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.abort(); updating = false; result = 1;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (updating) { httpOk = Update.end(true); updating = false; if (!httpOk) result = 1; }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (updating) Update.abort();
    updating = false; result = 1;
  }
}
void task(void *) {
  ArduinoOTA.setHostname("dragon-radar");
  ArduinoOTA.setPassword(password);
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.setTimeout(1500);
  ArduinoOTA.onStart([] {
    // ArduinoOTA calls this immediately after Update.begin and before any
    // payload is written. Refuse filesystem images; only app OTA is allowed.
    if (ArduinoOTA.getCommand() != U_FLASH) { result = 2; Update.abort(); return; }
    updating = true;
  });
  ArduinoOTA.onProgress([](unsigned n, unsigned total) { if (total && updating) progress = (int)(100ULL*n/total); });
  ArduinoOTA.onError([](ota_error_t error) { if (result != 2) result = 1; updating = false; if (error == OTA_AUTH_ERROR) stopRequested = true; });
  ArduinoOTA.onEnd([] { updating = false; });
  ArduinoOTA.begin();
  if (!httpConfigured) {
    const char *headers[] = {"X-OTA-Key", "X-OTA-MD5"};
    http.collectHeaders(headers, 2);
    http.on("/update", HTTP_POST, [] {
    if (!httpAuthorized) { http.send(403, "text/plain", "Access denied"); return; }
    if (!httpOk) { http.send(400, "text/plain", "Update failed"); return; }
    http.send(200, "text/plain", "Update OK; rebooting");
    delay(100); ESP.restart();
    }, httpUpload);
    httpConfigured = true;
  }
  http.begin();
  while (!stopRequested && WiFi.status() == WL_CONNECTED && (updating || (uint32_t)(millis()-started) < 60000UL)) {
    ArduinoOTA.handle();
    http.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  http.stop();
  ArduinoOTA.end();
  updating = false;
  active = false;
  vTaskDelete(nullptr);
}
}
bool otaServiceStart() {
  if (active || WiFi.status() != WL_CONNECTED) { result = 3; return false; }
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  if (!running || !next || running == next || next->size < 0x300000 ||
      !((running->address == 0x10000 && next->address == 0x310000) ||
        (running->address == 0x310000 && next->address == 0x10000))) { result = 4; return false; }
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  for (unsigned i=0;i<sizeof(password)-1;++i) password[i] = alphabet[esp_random() & 31];
  password[sizeof(password)-1] = 0;
  stopRequested = false; updating = false; progress = 0; result = 0; started = millis(); active = true;
  if (xTaskCreate(task,"ota",8192,nullptr,1,nullptr) != pdPASS) { active = false; result = 1; return false; }
  return true;
}
void otaServiceStop() { if (!updating) stopRequested = true; }
bool otaServiceActive() { return active; }
bool otaServiceUpdating() { return updating; }
const char *otaServicePassword() { return active ? password : ""; }
const char *otaServiceStatus() {
  if (updating) return "AGGIORNAMENTO";
  if (active) return "ATTIVO";
  switch (result.load()) { case 1: return "ERRORE"; case 2: return "SOLO APP"; case 3: return "NO WIFI"; case 4: return "PARTIZIONE ERRATA"; default: return "AVVIA"; }
}
