#pragma once
#include <cstdint>
#include <cstddef>
typedef int esp_err_t;
#define ESP_OK 0
enum esp_now_send_status_t {ESP_NOW_SEND_SUCCESS,ESP_NOW_SEND_FAIL};
struct esp_now_peer_info_t {uint8_t peer_addr[6];int ifidx;};
extern int mockSends;extern int mockError;
extern uint8_t mockPeer;
extern int mockAddError, mockAdds;
inline bool esp_now_is_peer_exist(const uint8_t *mac){return mac[0]==mockPeer;}

inline int esp_now_add_peer(const esp_now_peer_info_t *p){++mockAdds;if (!mockAddError) mockPeer=p->peer_addr[0];return mockAddError;}
inline int esp_now_send(const uint8_t*,const uint8_t*,size_t){++mockSends;return mockError;}
