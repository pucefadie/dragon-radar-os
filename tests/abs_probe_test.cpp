#include <cassert>
#include <string>
#include "Arduino.h"
#include "Preferences.h"
#include "WiFi.h"
#include "NetworkClientSecure.h"
#include "HTTPClient.h"
#include "audiobookshelf_client.h"
MockWifi WiFi;
MockSerial Serial;
std::string Preferences::origin, Preferences::pwdOrigin, Preferences::password;
int Preferences::passwordWrites=0;
bool NetworkClientSecure::bundleUsed=false;
int HTTPClient::response=200, HTTPClient::declaredSize=-1;
bool HTTPClient::disableRedirects=false;
std::string HTTPClient::body, HTTPClient::url;
bool HTTPClient::simulateRefresh=false; int HTTPClient::refreshGets=0;
std::string HTTPClient::refreshBody, HTTPClient::catalogBody;
static bool clockOk = true;
bool wifiServiceTimeReady() { return clockOk; }
int xTaskCreate(void (*task)(void *), const char *, unsigned, void *arg, unsigned, void *) { task(arg); return 1; }
void vTaskDelete(void *) {}
void vTaskDelay(unsigned) {}
int main() {
  audiobookshelfInit(); assert(audiobookshelfStatus() == AudiobookshelfState::NotConfigured);
  assert(!audiobookshelfSetOrigin("https://x.test/?token=secret"));
  assert(audiobookshelfSetOrigin("https://books.example"));
  assert(!audiobookshelfCheckServer() && audiobookshelfStatus() == AudiobookshelfState::WaitingWifi);
  WiFi.link=WL_CONNECTED; clockOk=false;
  assert(!audiobookshelfCheckServer() && audiobookshelfStatus() == AudiobookshelfState::WaitingTime);
  clockOk=true; HTTPClient::body="{\"isInit\":true}"; HTTPClient::declaredSize=(int)HTTPClient::body.size();
  assert(audiobookshelfCheckServer() && audiobookshelfStatus() == AudiobookshelfState::ServerReachable);
  assert(NetworkClientSecure::bundleUsed && HTTPClient::disableRedirects && HTTPClient::url == "https://books.example/status");
  HTTPClient::response=302; assert(audiobookshelfCheckServer() && audiobookshelfStatus() == AudiobookshelfState::AccessRequired);
  HTTPClient::response=200; HTTPClient::body="{\"isInit\":true}garbage"; HTTPClient::declaredSize=(int)HTTPClient::body.size();
  assert(audiobookshelfCheckServer() && audiobookshelfStatus() == AudiobookshelfState::ServerError);
  HTTPClient::body=std::string(2049,'x'); HTTPClient::declaredSize=-1;
  assert(audiobookshelfCheckServer() && audiobookshelfStatus() == AudiobookshelfState::ServerError);
  HTTPClient::response=-1; assert(audiobookshelfCheckServer() && audiobookshelfStatus() == AudiobookshelfState::NetworkError);
  HTTPClient::response=200; HTTPClient::body="{\"user\":{\"username\":\"dragon-user\",\"id\":\"test-user-id\",\"accessToken\":\"mock-token\",\"isActive\":true}}"; HTTPClient::declaredSize=(int)HTTPClient::body.size();
  assert(audiobookshelfLoginUser("synthetic-password") && audiobookshelfStatus() == AudiobookshelfState::Authenticated && audiobookshelfIsAuthenticated());
  assert(audiobookshelfPasswordSaved() && Preferences::password=="synthetic-password" && Preferences::pwdOrigin=="https://books.example");
  int writes=Preferences::passwordWrites;
  assert(audiobookshelfLoginUser("synthetic-password") && Preferences::passwordWrites==writes);
  audiobookshelfInit(); audiobookshelfUpdate(); assert(audiobookshelfIsAuthenticated() && Preferences::passwordWrites==writes);
  HTTPClient::body="{\"libraryItems\":[{\"id\":\"book_1\",\"mediaType\":\"book\",\"progressLastUpdate\":100,\"media\":{\"metadata\":{\"title\":\"Example\"}}}]}"; HTTPClient::declaredSize=(int)HTTPClient::body.size();
  assert(audiobookshelfLoadCatalog() && audiobookshelfStatus() == AudiobookshelfState::CatalogReady);
  AbsCatalog list{}; assert(audiobookshelfCopyCatalog(list) && list.count == 1 && std::string(list.items[0].title) == "Example");
  assert(HTTPClient::url == "https://books.example/api/me/items-in-progress?limit=8");
  HTTPClient::body="{\"user\":{\"username\":\"dragon-user\",\"id\":\"test-user-id\",\"accessToken\":\"old-token\",\"refreshToken\":\"old-refresh\"}}"; HTTPClient::declaredSize=(int)HTTPClient::body.size();
  assert(audiobookshelfLoginUser("synthetic-password") && audiobookshelfIsAuthenticated());
  HTTPClient::catalogBody="{\"libraryItems\":[{\"id\":\"book_2\",\"mediaType\":\"book\",\"progressLastUpdate\":101,\"media\":{\"metadata\":{\"title\":\"Refreshed\"}}}]}";
  HTTPClient::refreshBody="{\"user\":{\"username\":\"dragon-user\",\"id\":\"test-user-id\",\"accessToken\":\"new-token\",\"refreshToken\":\"new-refresh\"}}";
  HTTPClient::simulateRefresh=true; HTTPClient::refreshGets=0;
  assert(audiobookshelfLoadCatalog() && audiobookshelfStatus() == AudiobookshelfState::CatalogReady);
  assert(audiobookshelfCopyCatalog(list) && list.count == 1 && std::string(list.items[0].title) == "Refreshed");
  HTTPClient::refreshBody="{\"user\":{\"username\":\"OTHER\",\"id\":\"test-user-id\",\"accessToken\":\"bad-token\",\"refreshToken\":\"bad-refresh\"}}";
  HTTPClient::refreshGets=0;
  assert(audiobookshelfLoadCatalog() && audiobookshelfStatus() == AudiobookshelfState::AuthenticationFailed && !audiobookshelfIsAuthenticated());
  HTTPClient::simulateRefresh=false;
  HTTPClient::response=401;
  assert(!audiobookshelfLoadCatalog());
  assert(!audiobookshelfCopyCatalog(list) && list.count == 0);
  HTTPClient::response=200;
  HTTPClient::body="{\"user\":{\"username\":\"dragon-user\",\"id\":\"test-user-id\",\"accessToken\":\"mock-token\",\"isLocked\":true}}"; HTTPClient::declaredSize=(int)HTTPClient::body.size();
  assert(audiobookshelfLoginUser("synthetic-password") && audiobookshelfStatus() == AudiobookshelfState::WrongAccount && !audiobookshelfIsAuthenticated());
  HTTPClient::body="{\"user\":{\"username\":\"wrong\",\"id\":\"wrong_id\",\"accessToken\":\"mock-token\"}}"; HTTPClient::declaredSize=(int)HTTPClient::body.size();
  assert(audiobookshelfLoginUser("synthetic-password") && audiobookshelfStatus() == AudiobookshelfState::WrongAccount && !audiobookshelfIsAuthenticated());
  assert(!audiobookshelfCopyCatalog(list) && list.count == 0);
  HTTPClient::body="{\"user\":{\"username\":\"dragon-user\",\"id\":\"test-user-id\",\"accessToken\":\"bad\\r\\nHeader\"}}"; HTTPClient::declaredSize=(int)HTTPClient::body.size();
  assert(audiobookshelfLoginUser("synthetic-password") && audiobookshelfStatus() == AudiobookshelfState::ServerError && !audiobookshelfIsAuthenticated());
  HTTPClient::response=401;
  assert(audiobookshelfLoginUser("synthetic-password") && audiobookshelfStatus() == AudiobookshelfState::AuthenticationFailed && !audiobookshelfIsAuthenticated());
  assert(audiobookshelfSetOrigin("https://other.example") && !audiobookshelfPasswordSaved());
  audiobookshelfInit(); audiobookshelfUpdate(); assert(audiobookshelfStatus() == AudiobookshelfState::ReadyToCheck);
  assert(audiobookshelfForgetPassword() && Preferences::password.empty());
}
