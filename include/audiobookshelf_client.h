#pragma once
#include "abs_catalog.h"
// Development candidate: playback and synchronization are not implemented.
enum class AudiobookshelfState { NotConfigured, ReadyToCheck, Checking, WaitingWifi, WaitingTime, ServerReachable, AccessRequired, ServerError, NetworkError, Authenticating, Authenticated, AuthenticationFailed, WrongAccount, LoadingCatalog, CatalogReady, CatalogError };
void audiobookshelfInit();
void audiobookshelfUpdate();
bool audiobookshelfPasswordSaved();
bool audiobookshelfForgetPassword();
bool audiobookshelfSetOrigin(const char *url);
bool audiobookshelfCheckServer();
// After successful login, the user-authorized password is stored in NVS, bound to the server origin; it is never logged.
bool audiobookshelfLoginReady();
bool audiobookshelfLoginUser(const char *password);
bool audiobookshelfIsAuthenticated();
bool audiobookshelfLoadCatalog();
bool audiobookshelfCopyCatalog(AbsCatalog &out);
void audiobookshelfSelectNext();
unsigned audiobookshelfSelectedIndex();
AudiobookshelfState audiobookshelfStatus();
const char *audiobookshelfStatusText();
