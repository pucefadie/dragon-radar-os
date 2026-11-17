#pragma once
#define WL_CONNECTED 3
struct MockWifi { int link=0; int status() const { return link; } };
extern MockWifi WiFi;
