#pragma once
#include "esp_wifi.h"
struct wifi_pkt_rx_ctrl_t { int8_t rssi; };
struct esp_now_recv_info_t { const uint8_t *src_addr; const wifi_pkt_rx_ctrl_t *rx_ctrl; };
inline esp_err_t esp_now_init() { return ESP_OK; }
inline esp_err_t esp_now_deinit() { return ESP_OK; }
using MockRxCallback = void (*)(const esp_now_recv_info_t *, const uint8_t *, int);
extern MockRxCallback mockRxCallback;
inline esp_err_t esp_now_register_recv_cb(MockRxCallback callback) {
  mockRxCallback = callback;
  return ESP_OK;
}
