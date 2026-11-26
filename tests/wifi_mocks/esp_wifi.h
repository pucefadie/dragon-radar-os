#pragma once
#include <cstdint>
#define WIFI_IF_STA 0
#define ESP_OK 0
#define ESP_ERR_WIFI_NOT_CONNECT 1
enum wifi_storage_t { WIFI_STORAGE_FLASH, WIFI_STORAGE_RAM };
enum wifi_mode_t { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA };
enum wifi_second_chan_t { WIFI_SECOND_CHAN_NONE };
struct wifi_ap_record_t {};
struct wifi_config_t { struct { uint8_t ssid[32]; uint8_t password[64]; } sta; };
int esp_wifi_get_config(int, wifi_config_t *);
int esp_wifi_set_storage(wifi_storage_t);
int esp_wifi_get_mode(wifi_mode_t *);
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
int esp_wifi_set_channel(uint8_t, wifi_second_chan_t);
int esp_wifi_get_channel(uint8_t *, wifi_second_chan_t *);
