#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include "beacon_protocol.h"
// Each adapter owns one queue and permits at most one ESP-NOW send in flight.
struct DevTxPacket { uint8_t mac[6], bytes[RADIO_FRAME_SIZE]; uint32_t queuedAt; };
class DevRadioTx {
 public:
  bool init() { packets_=xQueueCreate(8,sizeof(DevTxPacket)); completions_=xQueueCreate(4,sizeof(esp_now_send_status_t)); return packets_ && completions_; }
  bool enqueue(const uint8_t *mac,const uint8_t *bytes) { if(!packets_)return false; DevTxPacket p;memcpy(p.mac,mac,6);memcpy(p.bytes,bytes,sizeof(p.bytes));p.queuedAt=millis();return xQueueSend(packets_,&p,0)==pdTRUE; }
  void completed(esp_now_send_status_t status) { if(!completions_ || xQueueSend(completions_,&status,0)!=pdTRUE) overflow_.store(true); }
  void tick() {
    esp_now_send_status_t status;
    while(xQueueReceive(completions_,&status,0)==pdTRUE) { inflight_=false;Serial.printf("[DEV INSECURE] TX driver=%s\n",status==ESP_NOW_SEND_SUCCESS?"OK":"FAIL"); }
    if(overflow_.exchange(false)) { inflight_=false;Serial.println("[DEV INSECURE] TX completion queue overflow"); }
    if(inflight_) {if(!stalled_ && uint32_t(millis()-inflightAt_)>=3000){stalled_=true;Serial.println("[DEV INSECURE] TX callback timeout; radio TX suspended");}return;}
    if(!packets_)return;
    DevTxPacket p;if(xQueueReceive(packets_,&p,0)!=pdTRUE)return;
    if(uint32_t(millis()-p.queuedAt)>=1000){Serial.println("[DEV INSECURE] TX queue expired");return;}
    if(!esp_now_is_peer_exist(p.mac)) { esp_now_peer_info_t peer={};memcpy(peer.peer_addr,p.mac,6);peer.ifidx=WIFI_IF_STA;if(esp_now_add_peer(&peer)!=ESP_OK){Serial.println("[DEV INSECURE] peer add failed");return;} }
    esp_err_t e=esp_now_send(p.mac,p.bytes,sizeof(p.bytes));inflight_=e==ESP_OK;if(inflight_){inflightAt_=millis();stalled_=false;}
    Serial.printf("[DEV INSECURE] TX driver enqueue=%d\n",int(e));
  }
 private:
  QueueHandle_t packets_=nullptr,completions_=nullptr; std::atomic<bool> overflow_{false};bool inflight_=false,stalled_=false;uint32_t inflightAt_=0;
};
