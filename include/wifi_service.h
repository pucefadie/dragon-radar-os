#pragma once
#include <stdint.h>
enum class WifiServiceState : uint8_t { Offline, Connecting, Connected, Provisioning, RetryWait };
void wifiServiceInit();
void wifiServiceBeginPortal();
void wifiServiceSetServerOriginHandler(bool (*handler)(const char *));
void wifiServiceUpdate();
bool wifiServicePortalActive();
const char *wifiServicePortalPassword();
WifiServiceState wifiServiceState();
const char *wifiServiceStatusText();
int32_t wifiServiceRssi();

bool wifiServiceTimeReady();
