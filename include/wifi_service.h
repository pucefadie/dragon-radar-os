#pragma once
#include <stdint.h>
enum class WifiServiceState : uint8_t { Offline, Connecting, Connected, Provisioning, RetryWait };
void wifiServiceInit();
#if RADAR_UI_TRIAL
bool wifiServicePrepareOfflineChannel(uint8_t channel);
#endif
void wifiServiceBeginPortal();
void wifiServiceSetServerOriginHandler(bool (*handler)(const char *));
void wifiServiceUpdate();
bool wifiServicePortalActive();
const char *wifiServicePortalPassword();
WifiServiceState wifiServiceState();
const char *wifiServiceStatusText();
int32_t wifiServiceRssi();

bool wifiServiceTimeReady();
