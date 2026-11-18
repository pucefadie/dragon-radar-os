#pragma once
#include <cstdint>
typedef int esp_err_t;
constexpr esp_err_t ESP_OK=0, ESP_ERR_INVALID_STATE=-1;
enum wifi_mode_t { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA };
enum wifi_second_chan_t { WIFI_SECOND_CHAN_NONE };
struct wifi_ap_record_t {};
inline esp_err_t esp_wifi_get_channel(uint8_t *c,wifi_second_chan_t*) { *c=6; return 0; }
inline esp_err_t esp_wifi_get_mode(wifi_mode_t *m) { *m=WIFI_MODE_STA; return 0; }
inline esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*) { return -1; }
