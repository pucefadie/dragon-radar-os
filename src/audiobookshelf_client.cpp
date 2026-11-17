#include "audiobookshelf_client.h"
#include "abs_url.h"
#include "wifi_service.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <cJSON.h>
#include <atomic>
#include <time.h>
#include <strings.h>
#include <new>
namespace {
char origin[128] = {};
char accessToken[2049] = {};
char refreshToken[2049] = {};
char expectedUserId[65] = {};
std::atomic<bool> authenticated{false};
std::atomic<bool> catalogValid{false};
std::atomic<bool> passwordSaved{false}, autoLoginPending{false};
std::atomic_flag catalogLock = ATOMIC_FLAG_INIT;
AbsCatalog catalog{};
std::atomic<unsigned> selectedIndex{0};
void clearBytes(void *p, size_t n) { volatile unsigned char *v=(volatile unsigned char *)p; while (n--) *v++=0; }
struct LoginRequest { char password[128]; };
bool savePassword(const char *password) {
  const size_t len=strlen(password); if (!len || len>=sizeof(LoginRequest::password) || !origin[0]) return false;
  Preferences p; if (!p.begin("abs_config",false)) return false;
  String bound=p.getString("pwd_origin", "");
  char previous[sizeof(LoginRequest::password)]={};
  size_t oldLen=p.getBytesLength("password");
  bool same=bound==origin && oldLen==len && p.getBytes("password",previous,len)==len && !memcmp(previous,password,len);
  clearBytes(previous,sizeof(previous));
  bool ok=same;
  if (!same) {
    if (bound!=origin) { p.remove("password"); if (p.putString("pwd_origin",origin)!=strlen(origin)) { p.end(); return false; } }
    ok=p.putBytes("password",password,len)==len;
  }
  p.end(); return ok;
}
bool safeBearerToken(const char *s) {
  if (!s || !*s) return false;
  size_t n = 0;
  for (; *s; ++s) { unsigned char c = (unsigned char)*s; if (++n >= sizeof(accessToken) || !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false; }
  return true;
}
void wipeJson(cJSON *node) {
  for (cJSON *p = node; p; p = p->next) {
    if (p->child) wipeJson(p->child);
    if (cJSON_IsString(p) && p->valuestring) clearBytes(p->valuestring, strlen(p->valuestring));
  }
}
std::atomic<AudiobookshelfState> state{AudiobookshelfState::NotConfigured};
template <size_t Max> class BoundedBody : public Stream {
public:
  char bytes[Max + 1] = {}; size_t length = 0;
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t write(uint8_t c) override { if (length >= Max) return 0; bytes[length++] = (char)c; bytes[length] = 0; return 1; }
  size_t write(const uint8_t *data, size_t n) override { if (n > Max - length) return 0; memcpy(bytes + length, data, n); length += n; bytes[length] = 0; return n; }
};
void checkTask(void *) {
  AudiobookshelfState result = AudiobookshelfState::NetworkError;
  NetworkClientSecure tls; tls.useBuiltinCACertBundle(); tls.setHandshakeTimeout(8);
  HTTPClient http; http.setConnectTimeout(6000); http.setTimeout(6000); http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  String url = String(origin) + "/status";
  if (http.begin(tls, url)) {
    http.addHeader("Accept", "application/json"); http.addHeader("Accept-Encoding", "identity");
    int status = http.GET();
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308 || status == 401 || status == 403) result = AudiobookshelfState::AccessRequired;
    else if (status == 200) {
      int length = http.getSize();
      if (length <= 2048) {
        BoundedBody<2048> body;
        int read = http.writeToStream(&body);
        if (read >= 0 && (size_t)read == body.length && (length < 0 || length == read)) {
          const char *end = nullptr;
          cJSON *doc = absJsonDepthSafe(body.bytes, body.length) ? cJSON_ParseWithLengthOpts(body.bytes, body.length, &end, 0) : nullptr;
          while (end && end < body.bytes + body.length && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
          cJSON *init = doc && end == body.bytes + body.length ? cJSON_GetObjectItemCaseSensitive(doc, "isInit") : nullptr;
          result = cJSON_IsTrue(init) ? AudiobookshelfState::ServerReachable : AudiobookshelfState::ServerError;
          cJSON_Delete(doc);
        } else result = AudiobookshelfState::ServerError;
      } else result = AudiobookshelfState::ServerError;
    } else if (status > 0) result = AudiobookshelfState::ServerError;
    http.end();
  }
  state.store(result);
  Serial.printf("[ABS] %s\n", audiobookshelfStatusText());
  vTaskDelete(nullptr);
}
void loginTask(void *parameter) {
  LoginRequest *request = static_cast<LoginRequest *>(parameter);
  AudiobookshelfState result = AudiobookshelfState::NetworkError;
  NetworkClientSecure tls; tls.useBuiltinCACertBundle(); tls.setHandshakeTimeout(8);
  HTTPClient http; http.setConnectTimeout(6000); http.setTimeout(6000); http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  cJSON *payload = cJSON_CreateObject(); char *encoded = nullptr;
  if (payload && cJSON_AddStringToObject(payload, "username", "dragon-user") && cJSON_AddStringToObject(payload, "password", request->password)) encoded = cJSON_PrintUnformatted(payload);
  if (payload) { cJSON *pass = cJSON_GetObjectItemCaseSensitive(payload, "password"); if (cJSON_IsString(pass) && pass->valuestring) clearBytes(pass->valuestring, strlen(pass->valuestring)); cJSON_Delete(payload); }
  if (encoded) {
    String url = String(origin) + "/login";
    if (http.begin(tls, url)) {
      http.addHeader("Accept", "application/json"); http.addHeader("Content-Type", "application/json"); http.addHeader("Accept-Encoding", "identity"); http.addHeader("x-return-tokens", "true");
      int status = http.POST((uint8_t *)encoded, strlen(encoded));
      clearBytes(encoded, strlen(encoded)); cJSON_free(encoded); encoded = nullptr;
      if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) result = AudiobookshelfState::AccessRequired;
      else if (status == 401 || status == 403) result = AudiobookshelfState::AuthenticationFailed;
      else if (status == 200) {
        BoundedBody<32768> *body = new (std::nothrow) BoundedBody<32768>();
        if (body && http.getSize() <= 32768) {
          int read = http.writeToStream(body);
          if (read >= 0 && (size_t)read == body->length && (http.getSize() < 0 || http.getSize() == read)) {
            const char *end = nullptr; cJSON *doc = absJsonDepthSafe(body->bytes, body->length) ? cJSON_ParseWithLengthOpts(body->bytes, body->length, &end, 0) : nullptr;
            while (end && end < body->bytes + body->length && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
            cJSON *user = doc && end == body->bytes + body->length ? cJSON_GetObjectItemCaseSensitive(doc, "user") : nullptr;
            cJSON *name = cJSON_GetObjectItemCaseSensitive(user, "username");
            cJSON *id = cJSON_GetObjectItemCaseSensitive(user, "id");
            cJSON *token = cJSON_GetObjectItemCaseSensitive(user, "accessToken");
            if (!cJSON_IsString(token)) token = cJSON_GetObjectItemCaseSensitive(user, "token");
            cJSON *refresh = cJSON_GetObjectItemCaseSensitive(user, "refreshToken");
            cJSON *active = cJSON_GetObjectItemCaseSensitive(user, "isActive");
            cJSON *locked = cJSON_GetObjectItemCaseSensitive(user, "isLocked");
            if (!cJSON_IsString(name) || !name->valuestring || strcmp(name->valuestring, "dragon-user") != 0 || !cJSON_IsString(id) || !id->valuestring || !id->valuestring[0] || strlen(id->valuestring)>=sizeof(expectedUserId) || cJSON_IsFalse(active) || cJSON_IsTrue(locked)) result = AudiobookshelfState::WrongAccount;
            else if (cJSON_IsString(token) && safeBearerToken(token->valuestring)) {
              if (cJSON_IsString(refresh) && !safeBearerToken(refresh->valuestring)) result = AudiobookshelfState::ServerError;
              else {
                strcpy(accessToken, token->valuestring); strcpy(expectedUserId,id->valuestring);
                if (cJSON_IsString(refresh)) strcpy(refreshToken,refresh->valuestring);
                authenticated.store(true); result = AudiobookshelfState::Authenticated;
              }
            } else result = AudiobookshelfState::ServerError;
            wipeJson(doc); cJSON_Delete(doc);
          } else result = AudiobookshelfState::ServerError;
        } else result = AudiobookshelfState::ServerError;
        if (body) { clearBytes(body->bytes, sizeof(body->bytes)); delete body; }
      } else if (status > 0) result = AudiobookshelfState::ServerError;
      http.end();
    }
    if (encoded) { clearBytes(encoded, strlen(encoded)); cJSON_free(encoded); }
  }
  if (result==AudiobookshelfState::Authenticated) {
    bool saved=savePassword(request->password); passwordSaved.store(saved);
    Serial.println(saved ? "[ABS] Accesso salvato sul dispositivo." : "[ABS] Accesso riuscito, salvataggio non riuscito.");
  }
  clearBytes(request,sizeof(*request)); free(request);
  state.store(result);
  Serial.printf("[ABS] %s\n", audiobookshelfStatusText());
  vTaskDelete(nullptr);
}
// Refresh is attempted only after a 401. Tokens remain in RAM and account identity
// is checked again before replacing either token. Never follow redirects.
bool refreshAccessToken() {
  if (!refreshToken[0] || !expectedUserId[0]) return false;
  bool ok=false;
  NetworkClientSecure tls; tls.useBuiltinCACertBundle(); tls.setHandshakeTimeout(8);
  HTTPClient http; http.setConnectTimeout(6000); http.setTimeout(6000); http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  String url=String(origin)+"/auth/refresh";
  if (http.begin(tls,url)) {
    http.addHeader("Accept","application/json"); http.addHeader("Accept-Encoding","identity");
    http.addHeader("x-refresh-token",refreshToken);
    int code=http.POST((uint8_t *)"",0);
    BoundedBody<32768> *body = nullptr;
    if (code==200 && http.getSize()<=32768) body=new (std::nothrow) BoundedBody<32768>();
    if (body) {
      int n=http.writeToStream(body);
      if (n>=0 && (size_t)n==body->length && (http.getSize()<0 || http.getSize()==n)) {
        const char *end=nullptr;
        cJSON *doc=absJsonDepthSafe(body->bytes,body->length)?cJSON_ParseWithLengthOpts(body->bytes,body->length,&end,0):nullptr;
        while(end && end<body->bytes+body->length && (*end==' '||*end=='\n'||*end=='\r'||*end=='\t')) ++end;
        cJSON *user=doc && end==body->bytes+body->length ? cJSON_GetObjectItemCaseSensitive(doc,"user") : nullptr;
        cJSON *name=cJSON_GetObjectItemCaseSensitive(user,"username"), *id=cJSON_GetObjectItemCaseSensitive(user,"id");
        cJSON *token=cJSON_GetObjectItemCaseSensitive(user,"accessToken"), *refresh=cJSON_GetObjectItemCaseSensitive(user,"refreshToken");
        cJSON *active=cJSON_GetObjectItemCaseSensitive(user,"isActive"), *locked=cJSON_GetObjectItemCaseSensitive(user,"isLocked");
        if(cJSON_IsString(name) && name->valuestring && !strcmp(name->valuestring,"dragon-user") && cJSON_IsString(id) && id->valuestring && !strcmp(id->valuestring,expectedUserId)
           && !cJSON_IsFalse(active) && !cJSON_IsTrue(locked) && cJSON_IsString(token) && safeBearerToken(token->valuestring)
           && cJSON_IsString(refresh) && safeBearerToken(refresh->valuestring)) {
          clearBytes(accessToken,sizeof(accessToken)); clearBytes(refreshToken,sizeof(refreshToken));
          strcpy(accessToken,token->valuestring); strcpy(refreshToken,refresh->valuestring); ok=true;
        }
        wipeJson(doc); cJSON_Delete(doc);
      }
      clearBytes(body->bytes,sizeof(body->bytes)); delete body;
    }
    http.end();
  }
  return ok;
}
void catalogTask(void *) {
  AudiobookshelfState result = AudiobookshelfState::NetworkError;
  NetworkClientSecure tls; tls.useBuiltinCACertBundle(); tls.setHandshakeTimeout(8);
  HTTPClient http; http.setConnectTimeout(6000); http.setTimeout(6000); http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  String url = String(origin) + "/api/me/items-in-progress?limit=8";
  if (http.begin(tls, url)) {
    http.addHeader("Accept", "application/json"); http.addHeader("Accept-Encoding", "identity");
    http.addHeader("Authorization", String("Bearer ") + accessToken);
    int status = http.GET();
    if (status == 401 && refreshToken[0]) {
      http.end();
      if (refreshAccessToken() && http.begin(tls,url)) {
        http.addHeader("Accept","application/json"); http.addHeader("Accept-Encoding","identity");
        http.addHeader("Authorization",String("Bearer ")+accessToken); status=http.GET();
      }
    }
    if (status == 401 || status == 403) { clearBytes(accessToken, sizeof(accessToken)); clearBytes(refreshToken,sizeof(refreshToken)); clearBytes(expectedUserId,sizeof(expectedUserId)); authenticated.store(false); catalogValid.store(false); result = AudiobookshelfState::AuthenticationFailed; }
    else if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) result = AudiobookshelfState::AccessRequired;
    else if (status == 200) {
      BoundedBody<65536> *body = new (std::nothrow) BoundedBody<65536>();
      AbsCatalog *parsed = new (std::nothrow) AbsCatalog();
      if (body && parsed && http.getSize() <= 65536) {
        int read = http.writeToStream(body);
        if (read >= 0 && (size_t)read == body->length && (http.getSize() < 0 || http.getSize() == read) && absParseCatalog(body->bytes, body->length, 8, parsed)) {
          while (catalogLock.test_and_set(std::memory_order_acquire)) vTaskDelay(1);
          catalog = *parsed; selectedIndex.store(0); catalogValid.store(true); catalogLock.clear(std::memory_order_release); result = AudiobookshelfState::CatalogReady;
        } else result = AudiobookshelfState::CatalogError;
      } else result = AudiobookshelfState::CatalogError;
      if (body) { clearBytes(body->bytes, sizeof(body->bytes)); delete body; }
      delete parsed;
    } else if (status > 0) result = AudiobookshelfState::ServerError;
    http.end();
  }
  state.store(result); Serial.printf("[ABS] %s\n", audiobookshelfStatusText());
  vTaskDelete(nullptr);
}
}
void audiobookshelfInit() {
  Preferences p;
  if (p.begin("abs_config", true)) {
    String value=p.getString("origin", ""), bound=p.getString("pwd_origin", "");
    absNormalizeHttpsOrigin(value.c_str(), origin, sizeof(origin));
    size_t length=p.getBytesLength("password");
    bool exists=origin[0] && bound==origin && length>0 && length<sizeof(LoginRequest::password);
    passwordSaved.store(exists); autoLoginPending.store(exists); p.end();
  }
  state.store(origin[0] ? AudiobookshelfState::ReadyToCheck : AudiobookshelfState::NotConfigured);
}
void audiobookshelfUpdate() {
  if (!autoLoginPending.load() || !origin[0] || WiFi.status()!=WL_CONNECTED || !wifiServiceTimeReady()) return;
  AudiobookshelfState current=state.load();
  if (current==AudiobookshelfState::Checking || current==AudiobookshelfState::Authenticating || current==AudiobookshelfState::LoadingCatalog) return;
  autoLoginPending.store(false);
  char password[sizeof(LoginRequest::password)]={};
  Preferences p; if (!p.begin("abs_config",true)) return;
  String bound=p.getString("pwd_origin", ""); size_t n=p.getBytesLength("password");
  bool valid=bound==origin && n>0 && n<sizeof(password) && p.getBytes("password",password,n)==n;
  p.end();
  if (valid) { password[n]=0; if (strlen(password)==n) audiobookshelfLoginUser(password); }
  clearBytes(password,sizeof(password));
}
bool audiobookshelfPasswordSaved() { return passwordSaved.load(); }
bool audiobookshelfForgetPassword() {
  Preferences p; if (!p.begin("abs_config",false)) return false;
  bool ok=p.remove("password"); p.remove("pwd_origin"); p.end();
  if (ok) { passwordSaved.store(false); autoLoginPending.store(false); }
  return ok;
}
bool audiobookshelfSetOrigin(const char *url) {
  if (state.load() == AudiobookshelfState::Checking || state.load() == AudiobookshelfState::Authenticating || state.load() == AudiobookshelfState::LoadingCatalog) return false;
  char normalized[sizeof(origin)]; if (!absNormalizeHttpsOrigin(url, normalized, sizeof(normalized))) return false;
  Preferences p; if (!p.begin("abs_config", false)) return false;
  size_t written = p.putString("origin", normalized); p.end();
  if (written != strlen(normalized)) return false;
  clearBytes(accessToken, sizeof(accessToken)); clearBytes(refreshToken,sizeof(refreshToken)); clearBytes(expectedUserId,sizeof(expectedUserId)); authenticated.store(false); catalogValid.store(false);
  if (!catalogLock.test_and_set(std::memory_order_acquire)) { memset(&catalog, 0, sizeof(catalog)); catalogLock.clear(std::memory_order_release); }
  memcpy(origin, normalized, strlen(normalized)+1);
  Preferences credentials; bool saved=false;
  if (credentials.begin("abs_config",true)) { String bound=credentials.getString("pwd_origin", ""); size_t n=credentials.getBytesLength("password"); saved=bound==origin && n>0 && n<sizeof(LoginRequest::password); credentials.end(); }
  passwordSaved.store(saved); autoLoginPending.store(saved);
  state.store(AudiobookshelfState::ReadyToCheck); return true;
}
bool audiobookshelfCheckServer() {
  if (!origin[0] || state.load() == AudiobookshelfState::Checking || state.load() == AudiobookshelfState::Authenticating || state.load() == AudiobookshelfState::LoadingCatalog) return false;
  if (WiFi.status() != WL_CONNECTED) { state.store(AudiobookshelfState::WaitingWifi); return false; }
  // Time is needed for certificate validation. No RTC or GNSS date is trusted here.
  time_t now = time(nullptr); if (!wifiServiceTimeReady() || now < 1704067200 || now > 2524608000LL) { state.store(AudiobookshelfState::WaitingTime); return false; }
  state.store(AudiobookshelfState::Checking);
  if (xTaskCreate(checkTask, "abs_probe", 8192, nullptr, 1, nullptr) != pdPASS) { state.store(AudiobookshelfState::NetworkError); return false; }
  return true;
}
bool audiobookshelfLoginReady() {
  if (!origin[0]) { state.store(AudiobookshelfState::NotConfigured); return false; }
  if (state.load() == AudiobookshelfState::Checking || state.load() == AudiobookshelfState::Authenticating || state.load() == AudiobookshelfState::LoadingCatalog) return false;
  if (WiFi.status() != WL_CONNECTED) { state.store(AudiobookshelfState::WaitingWifi); return false; }
  time_t now = time(nullptr); if (!wifiServiceTimeReady() || now < 1704067200 || now > 2524608000LL) { state.store(AudiobookshelfState::WaitingTime); return false; }
  return true;
}
bool audiobookshelfLoginUser(const char *password) {
  if (!password || !*password || strlen(password) >= sizeof(LoginRequest::password) || !audiobookshelfLoginReady()) return false;
  LoginRequest *request = static_cast<LoginRequest *>(calloc(1, sizeof(LoginRequest))); if (!request) return false;
  autoLoginPending.store(false);
  strncpy(request->password, password, sizeof(request->password)-1);
  clearBytes(accessToken, sizeof(accessToken)); clearBytes(refreshToken,sizeof(refreshToken)); clearBytes(expectedUserId,sizeof(expectedUserId)); authenticated.store(false); catalogValid.store(false);
  if (!catalogLock.test_and_set(std::memory_order_acquire)) { memset(&catalog, 0, sizeof(catalog)); catalogLock.clear(std::memory_order_release); }
  state.store(AudiobookshelfState::Authenticating);
  if (xTaskCreate(loginTask, "abs_login", 8192, request, 1, nullptr) != pdPASS) { clearBytes(request, sizeof(*request)); free(request); state.store(AudiobookshelfState::NetworkError); return false; }
  return true;
}
bool audiobookshelfIsAuthenticated() { return authenticated.load(); }
bool audiobookshelfLoadCatalog() {
  if (!authenticated.load() || !origin[0] || state.load() == AudiobookshelfState::Checking || state.load() == AudiobookshelfState::Authenticating || state.load() == AudiobookshelfState::LoadingCatalog) return false;
  if (WiFi.status() != WL_CONNECTED) { state.store(AudiobookshelfState::WaitingWifi); return false; }
  time_t now = time(nullptr); if (!wifiServiceTimeReady() || now < 1704067200 || now > 2524608000LL) { state.store(AudiobookshelfState::WaitingTime); return false; }
  state.store(AudiobookshelfState::LoadingCatalog);
  if (xTaskCreate(catalogTask, "abs_catalog", 8192, nullptr, 1, nullptr) != pdPASS) { state.store(AudiobookshelfState::NetworkError); return false; }
  return true;
}
bool audiobookshelfCopyCatalog(AbsCatalog &out) {
  if (!authenticated.load() || !catalogValid.load()) { memset(&out, 0, sizeof(out)); return false; }
  if (catalogLock.test_and_set(std::memory_order_acquire)) return false;
  out = catalog; catalogLock.clear(std::memory_order_release); return true;
}
void audiobookshelfSelectNext() {
  if (!authenticated.load() || catalogLock.test_and_set(std::memory_order_acquire)) return;
  if (catalog.count) selectedIndex.store((selectedIndex.load() + 1) % catalog.count);
  catalogLock.clear(std::memory_order_release);
}
unsigned audiobookshelfSelectedIndex() { return selectedIndex.load(); }
AudiobookshelfState audiobookshelfStatus() { return state.load(); }
const char *audiobookshelfStatusText() {
  switch (state.load()) {
    case AudiobookshelfState::LoadingCatalog: return "CARICO LIBRI...";
    case AudiobookshelfState::CatalogReady: return "LIBRI CARICATI";
    case AudiobookshelfState::CatalogError: return "ERRORE CATALOGO";
    case AudiobookshelfState::Authenticating: return "ACCESSO dragon-user...";
    case AudiobookshelfState::Authenticated: return "dragon-user ACCESSO OK";
    case AudiobookshelfState::AuthenticationFailed: return "ACCESSO FALLITO";
    case AudiobookshelfState::WrongAccount: return "UTENTE DIVERSO DA dragon-user";
    case AudiobookshelfState::ReadyToCheck: return "SERVER DA VERIFICARE";
    case AudiobookshelfState::Checking: return "VERIFICA HTTPS...";
    case AudiobookshelfState::WaitingWifi: return "WIFI NON CONNESSO";
    case AudiobookshelfState::WaitingTime: return "ATTESA ORA DI RETE";
    case AudiobookshelfState::ServerReachable: return "SERVER RAGGIUNGIBILE";
    case AudiobookshelfState::AccessRequired: return "REDIRECT O ACCESSO";
    case AudiobookshelfState::ServerError: return "RISPOSTA NON VALIDA";
    case AudiobookshelfState::NetworkError: return "ERRORE RETE/TLS";
    default: return "SERVER NON CONFIGURATO";
  }
}
