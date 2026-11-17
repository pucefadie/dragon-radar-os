#pragma once
#include <cstdint>
#define WIFI_IF_STA 0
#define ESP_OK 0
struct wifi_config_t { struct { uint8_t ssid[32]; uint8_t password[64]; } sta; };
int esp_wifi_get_config(int, wifi_config_t *);
