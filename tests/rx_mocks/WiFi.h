#pragma once
using WiFiEvent_t=int;
constexpr int ARDUINO_EVENT_WIFI_STA_CONNECTED=1, ARDUINO_EVENT_WIFI_STA_DISCONNECTED=2;
struct WifiMock { void onEvent(void (*)(WiFiEvent_t)) {} };
static WifiMock WiFi;
