#include "ota_service.h"
#include "radar_ota_format.h"
#include "wifi_service.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_app_desc.h>
#include <esp_flash.h>
#include <esp_flash_encrypt.h>
#include <esp_secure_boot.h>
#include <esp_http_server.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <cerrno>
#include <sys/socket.h>

#ifndef RADAR_OTA_VERSION
#define RADAR_OTA_VERSION 1
#endif
#ifndef RADAR_UI_TRIAL
#define RADAR_UI_TRIAL 0
#endif
#ifndef RADIO_DEV_INSECURE
#define RADIO_DEV_INSECURE 0
#endif

#ifndef RADAR_OTA_NVS_GUARD
#define RADAR_OTA_NVS_GUARD 0
#endif
#define RADAR_OTA_SUPPORTED (RADAR_OTA_NVS_GUARD && !RADIO_DEV_INSECURE && \
    CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE && CONFIG_SPIRAM_MODE_OCT && CONFIG_ESPTOOLPY_FLASHMODE_QIO)

// Arduino-ESP32 3.3.12 otherwise confirms PENDING_VERIFY before setup().
#if RADAR_OTA_SUPPORTED
extern "C" bool verifyRollbackLater() { return true; }
#endif

namespace {
using namespace radarOta;
using State = RadarOtaState;
#if RADAR_OTA_SUPPORTED
__attribute__((used, aligned(4), section(".rodata_custom_desc")))
const ImageIdentity imageIdentity = {"DragonRadar-M3f", RADAR_OTA_VERSION};
#else
const ImageIdentity imageIdentity = {"OTA-disabled", 0};
#endif
static_assert(RADAR_OTA_VERSION > 0 && RADAR_OTA_VERSION <= versionLimit);
static_assert(descriptorOffset == sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t));

std::atomic<State> state{State::Idle};
std::atomic<unsigned> progress{0};
std::atomic<uint32_t> uploadAt{0}, rebootAt{0};
httpd_handle_t server = nullptr;
const esp_partition_t *target = nullptr;
uint32_t started = 0, apAddress = 0;
uint8_t channel = 0;
char password[17]{}, manifestKeyHex[33]{};
uint8_t manifestKey[16]{};
esp_timer_handle_t bootTimer = nullptr;
bool pendingBoot = false;

bool live(State value) {
  return value == State::Ready || value == State::Receiving || value == State::Committing ||
      value == State::Rebooting;
}

bool flashMatches(uint32_t offset, size_t length, const char *expected) {
  uint8_t bytes[512], digest[32];
  char encoded[65];
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
  while (ok && length) {
    const size_t n = std::min(length, sizeof(bytes));
    ok = esp_flash_read(nullptr, bytes, offset, n) == ESP_OK &&
        mbedtls_sha256_update(&hash, bytes, n) == 0;
    offset += n;
    length -= n;
  }
  ok = ok && mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  if (!ok) return false;
  hex(digest, sizeof(digest), encoded);
  return same(encoded, expected, 64);
}

bool compatible() {
#if !RADAR_OTA_SUPPORTED
  return false;
#else
  static const bool checked = [] {
    uint32_t capacity = 0;
    return !esp_secure_boot_enabled() && !esp_flash_encryption_enabled() &&
        esp_flash_get_size(nullptr, &capacity) == ESP_OK && capacity == 0x1000000 &&
        flashMatches(0, 0x8000, "925239a911e376c690218a5b91341555f6ff447a37b333069df630770cf6f9ae") &&
        flashMatches(0x8000, 0xc00, "ace02503447d0f470692e65fa76002f2d77a92dc81cd3813d8aa66718d716da9");
  }();
  return checked;
#endif
}

bool ota0Released(const esp_partition_t *partition) {
  char bytes[sizeof(permit)]{};
  return partition->address != ota0 ||
      (esp_partition_read(partition, permitOffset, bytes, sizeof(bytes)) == ESP_OK &&
       memcmp(bytes, permit, sizeof(bytes)) == 0);
}

bool verifyManifest(httpd_req_t *req, Manifest &manifest) {
  char header[112], signature[65], expected[65];
  uint8_t tag[32];
  if (httpd_req_get_hdr_value_str(req, "X-Manifest", header, sizeof(header)) != ESP_OK ||
      httpd_req_get_hdr_value_str(req, "X-Signature", signature, sizeof(signature)) != ESP_OK ||
      !lowercaseHex(signature, 64) || !parseManifest(header, req->content_len, imageIdentity.version, manifest))
    return false;
  if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), manifestKey, sizeof(manifestKey),
                     reinterpret_cast<const uint8_t *>(header), strlen(header), tag) != 0)
    return false;
  hex(tag, sizeof(tag), expected);
  return same(expected, signature, 64);
}

bool validPrefix(const uint8_t *bytes, const Manifest &manifest) {
  esp_image_header_t header{};
  ImageIdentity id{};
  memcpy(&header, bytes, sizeof(header));
  memcpy(&id, bytes + descriptorOffset, sizeof(id));
  return header.magic == ESP_IMAGE_HEADER_MAGIC && header.chip_id == ESP_CHIP_ID_ESP32S3 &&
      header.spi_mode == ESP_IMAGE_SPI_MODE_DIO && header.spi_size == ESP_IMAGE_FLASH_SIZE_16MB &&
      header.spi_speed == ESP_IMAGE_SPI_SPEED_DIV_1 && header.hash_appended == 1 &&
      validIdentity(id) && id.version == manifest.version;
}

bool receiving() {
  if (state != State::Receiving) return false;
  if (!elapsed(millis(), uploadAt.load(), uploadMs)) return true;
  State expected = State::Receiving;
  state.compare_exchange_strong(expected, State::Timeout);
  return false;
}

esp_err_t receiveImage(httpd_req_t *req, const Manifest &manifest) {
  uint8_t bytes[2048], digest[32];
  constexpr size_t prefixSize = descriptorOffset + sizeof(ImageIdentity);
  size_t used = 0, written = 0;
  esp_ota_handle_t handle = 0;
  bool writing = false;
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
  while (ok && written < manifest.size && receiving()) {
    const int got = httpd_req_recv(req, reinterpret_cast<char *>(bytes + used),
        std::min(sizeof(bytes) - used, size_t(manifest.size - written) - used));
    if (got <= 0) break;
    used += size_t(got);
    if (!writing) {
      if (used < prefixSize) continue;
      if (!receiving() || !validPrefix(bytes, manifest) ||
          esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) break;
      writing = true;
    }
    ok = receiving() && esp_ota_write(handle, bytes, used) == ESP_OK &&
        mbedtls_sha256_update(&hash, bytes, used) == 0;
    if (!ok) break;
    written += used;
    progress = unsigned(written * 100 / manifest.size);
    used = 0;
  }
  ok = ok && writing && written == manifest.size && receiving() &&
      mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  if (ok) {
    char encoded[65];
    hex(digest, sizeof(digest), encoded);
    ok = same(encoded, manifest.digest, 64);
  }
  if (ok && receiving()) {
    writing = false; // esp_ota_end releases the handle even on error.
    ok = esp_ota_end(handle) == ESP_OK;
    State expected = State::Receiving;
    ok = ok && receiving() && state.compare_exchange_strong(expected, State::Committing) &&
        esp_ota_set_boot_partition(target) == ESP_OK;
  } else {
    ok = false;
  }
  if (writing) esp_ota_abort(handle);
  if (ok) {
    const esp_err_t sent = httpd_resp_sendstr(req, "Immagine verificata. Radar in riavvio.");
    rebootAt = millis();
    state = State::Rebooting;
    return sent;
  }
  httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Immagine rifiutata o trasferimento interrotto");
  State expected = State::Receiving;
  if (!state.compare_exchange_strong(expected, State::Rejected) && expected == State::Committing)
    state = State::Error;
  return ESP_FAIL;
}

int receiveSocket(httpd_handle_t, int socket, char *buffer, size_t size, int flags) {
  const auto current = state.load();
  if ((current == State::Ready && elapsed(millis(), started, idleMs)) ||
      (current == State::Receiving && !receiving()) ||
      (current != State::Ready && current != State::Receiving)) return HTTPD_SOCK_ERR_FAIL;
  const int result = recv(socket, buffer, size, flags);
  if (result >= 0) return result;
  return (errno == EAGAIN || errno == EWOULDBLOCK) ? HTTPD_SOCK_ERR_TIMEOUT : HTTPD_SOCK_ERR_FAIL;
}

// Only accept sockets addressed to the maintenance AP, even if STA gains an address.
esp_err_t openClient(httpd_handle_t handle, int socket) {
  sockaddr_storage address{};
  socklen_t length = sizeof(address);
  if (getsockname(socket, reinterpret_cast<sockaddr *>(&address), &length) != 0) return ESP_FAIL;
  bool local = false;
  if (address.ss_family == AF_INET)
    local = reinterpret_cast<sockaddr_in *>(&address)->sin_addr.s_addr == apAddress;
  if (address.ss_family == AF_INET6) {
    const auto &ip = reinterpret_cast<sockaddr_in6 *>(&address)->sin6_addr;
    local = IN6_IS_ADDR_V4MAPPED(&ip) && memcmp(ip.s6_addr + 12, &apAddress, 4) == 0;
  }
  return local ? httpd_sess_set_recv_override(handle, socket, receiveSocket) : ESP_FAIL;
}

esp_err_t showPage(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  constexpr char page[] = R"HTML(<!doctype html><html lang=it><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>DragonRadar OTA</title>
<h1>DragonRadar OTA</h1><p>Versione %lu</p>
<label>Manifest autenticato <input id=m type=file accept=.json></label><br>
<label>Firmware S3 <input id=f type=file accept=.bin></label><br>
<button id=b>Carica</button> <progress id=p max=100 value=0></progress><pre id=o></pre>
<script>b.onclick=async()=>{try{const a=JSON.parse(await m.files[0].text());const x=new XMLHttpRequest();
x.open('POST','/firmware');x.setRequestHeader('X-Manifest',a.manifest);x.setRequestHeader('X-Signature',a.signature);
x.setRequestHeader('Content-Type','application/octet-stream');x.upload.onprogress=e=>p.value=100*e.loaded/e.total;
x.onload=()=>{o.textContent=x.status+' '+x.responseText;b.disabled=false};
x.onerror=()=>{o.textContent='Connessione terminata. Controlla il radar.';b.disabled=false};
b.disabled=true;x.send(f.files[0]);}catch(e){o.textContent=e;}}</script></html>)HTML";
  char body[sizeof(page) + 12];
  snprintf(body, sizeof(body), page, (unsigned long)imageIdentity.version);
  return httpd_resp_sendstr(req, body);
}

esp_err_t upload(httpd_req_t *req) {
  Manifest manifest{};
  if (!verifyManifest(req, manifest)) {
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Manifest non valido");
    return ESP_FAIL;
  }
  uploadAt = millis();
  State expected = State::Ready;
  if (elapsed(uploadAt.load(), started, idleMs) || !state.compare_exchange_strong(expected, State::Receiving)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Sessione non disponibile");
    return ESP_FAIL;
  }
  return receiveImage(req, manifest);
}

void wipeSession() {
  mbedtls_platform_zeroize(manifestKey, sizeof(manifestKey));
  mbedtls_platform_zeroize(manifestKeyHex, sizeof(manifestKeyHex));
  mbedtls_platform_zeroize(password, sizeof(password));
}

void stopNetwork() {
  if (server) { httpd_stop(server); server = nullptr; }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  if (channel) esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  if (!RADAR_UI_TRIAL) esp_wifi_set_storage(WIFI_STORAGE_FLASH);
  wipeSession();
}

void bootDeadline(void *) {
  esp_restart();
}
}

void otaServiceBeginBoot() {
  esp_ota_img_states_t status;
  if (!RADAR_OTA_SUPPORTED || pendingBoot || esp_ota_get_state_partition(esp_ota_get_running_partition(), &status) != ESP_OK ||
      status != ESP_OTA_IMG_PENDING_VERIFY) return;
  pendingBoot = true;
  esp_timer_create_args_t args{};
  args.callback = bootDeadline;
  args.name = "radar_ota_boot";
  if (esp_timer_create(&args, &bootTimer) != ESP_OK ||
      esp_timer_start_once(bootTimer, 25000000) != ESP_OK) esp_restart();
}

bool otaServiceBootPending() { return pendingBoot; }

void otaServiceFinishBoot(bool healthy) {
  if (!pendingBoot) return;
  if (!healthy || !compatible() || !psramFound() ||
      esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    esp_ota_mark_app_invalid_rollback_and_reboot();
    esp_restart();
  }
  esp_timer_stop(bootTimer);
  esp_timer_delete(bootTimer);
  pendingBoot = false;
  Serial.println("RADAR_OTA_BOOT_VALID");
}

bool otaServiceStart() {
  if (otaServiceActive()) return false;
  progress = 0;
  const auto *running = esp_ota_get_running_partition();
  target = esp_ota_get_next_update_partition(nullptr);
  const auto *boot = esp_ota_get_boot_partition();
  esp_ota_img_states_t status;
  if (!compatible() || !psramFound() || !running || !target || !boot ||
      boot->address != running->address || running->size != slotSize || target->size != slotSize ||
      esp_ota_get_state_partition(running, &status) != ESP_OK || status != ESP_OTA_IMG_VALID ||
      target->address != nextSlot(running->address)) {
    state = State::BootIncompatible;
    return false;
  }
  if (!mayWrite(running->address, target->address, ota0Released(target))) {
    state = State::Protected;
    return false;
  }
  wifi_mode_t mode{};
  wifi_second_chan_t secondary{};
  wifi_ap_record_t ap{};
  if (wifiServicePortalActive() ||
      (wifiServiceState() == WifiServiceState::Connecting && !RADAR_UI_TRIAL) || esp_wifi_get_mode(&mode) != ESP_OK || mode != WIFI_MODE_STA ||
      esp_wifi_sta_get_ap_info(&ap) != ESP_ERR_WIFI_NOT_CONNECT ||
      esp_wifi_get_channel(&channel, &secondary) != ESP_OK || !channel) {
    state = State::BusyWifi;
    return false;
  }
  // Credentials are temporary and independent of DRA1 and Wi-Fi settings.
  uint8_t passBytes[8];
  esp_fill_random(passBytes, sizeof(passBytes));
  hex(passBytes, sizeof(passBytes), password);
  mbedtls_platform_zeroize(passBytes, sizeof(passBytes));
  esp_fill_random(manifestKey, sizeof(manifestKey));
  hex(manifestKey, sizeof(manifestKey), manifestKeyHex);
  if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
    wipeSession();
    state = State::Error;
    return false;
  }
  if (!WiFi.softAP("DragonRadar", password, channel, false, 1, false, WIFI_AUTH_WPA2_PSK,
                   WIFI_CIPHER_TYPE_CCMP)) {
    stopNetwork();
    state = State::Error;
    return false;
  }
  apAddress = uint32_t(WiFi.softAPIP());
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 10240;
  config.max_open_sockets = 2;
  config.recv_wait_timeout = 2;
  config.send_wait_timeout = 2;
  config.open_fn = openClient;
  const httpd_uri_t home = {"/", HTTP_GET, showPage, nullptr};
  const httpd_uri_t post = {"/firmware", HTTP_POST, upload, nullptr};
  started = millis();
  state = State::Ready;
  if (apAddress != uint32_t(IPAddress(192, 168, 4, 1)) || httpd_start(&server, &config) != ESP_OK ||
      httpd_register_uri_handler(server, &home) != ESP_OK ||
      httpd_register_uri_handler(server, &post) != ESP_OK) {
    state = State::Error;
    stopNetwork();
    return false;
  }
  return true;
}

void otaServiceStop() {
  State current = state.load();
  while ((current == State::Ready || current == State::Receiving) &&
      !state.compare_exchange_weak(current, State::Cancelled)) {}
}

void otaServiceUpdate() {
  State current = state.load();
  if (current == State::Ready && elapsed(millis(), started, idleMs))
    state.compare_exchange_strong(current, State::Timeout);
  if (current == State::Receiving) (void)receiving();
  if (state == State::Rebooting && elapsed(millis(), rebootAt.load(), 250)) esp_restart();
  if (server && !live(state.load())) stopNetwork();
}

bool otaServiceActive() { return server || live(state.load()); }
uint32_t otaServiceVersion() {
  // Keep the descriptor in the image even with linker garbage collection.
  const volatile ImageIdentity &embedded = imageIdentity;
  return embedded.version;
}
unsigned otaServiceProgress() { return progress; }
RadarOtaState otaServiceState() { return state; }
const char *otaServicePassword() { return password; }
const char *otaServiceManifestKey() { return manifestKeyHex; }
const char *otaServiceStatus() {
  switch (state.load()) {
    case State::Ready: return "AP ATTIVO";
    case State::Receiving: return "RICEZIONE";
    case State::Cancelled: return "ANNULLATO";
    case State::Timeout: return "TEMPO SCADUTO";
    case State::Rejected: return "UPLOAD RIFIUTATO";
    case State::Committing: return "SELEZIONE SLOT";
    case State::Rebooting: return "RIAVVIO";
    case State::BootIncompatible: return "BOOT NON COMPATIBILE";
    case State::BusyWifi: return "SERVE RADAR OFFLINE";
    case State::Protected: return "OTA0 ORIGINALE PROTETTA";
    case State::Error: return "ERRORE OTA";
    default: return "PRONTO";
  }
}
