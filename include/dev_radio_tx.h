#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include "secure_radio.h"

// Access to the queue is nonblocking. Only the loop sends; only its one TX
// completion can release the in-flight slot. A timeout does not reuse the slot.
struct DevTxPacket { uint8_t mac[6], bytes[SECURE_RADIO_SIZE], size; uint32_t queuedAt; };
struct SecureTxDiagnostics {
  uint32_t enqueued=0, queueFull=0, expired=0, missingPeer=0;
  uint32_t submitted=0, apiRejected=0, delivered=0, deliveryFailed=0;
  esp_err_t lastApiError=ESP_OK;
};
class DevRadioTx {
 public:
  bool init(bool allowDevPeer=true) {
    allowDevPeer_=allowDevPeer;
    if (!packets_) packets_=xQueueCreate(8,sizeof(DevTxPacket));
    return packets_!=nullptr;
  }
  bool enqueue(const uint8_t *mac,const uint8_t *bytes,size_t size=RADIO_FRAME_SIZE) {
    if (!packets_ || !mac || !bytes || !size || size>SECURE_RADIO_SIZE) return false;
    DevTxPacket packet{};
    memcpy(packet.mac,mac,6);
    memcpy(packet.bytes,bytes,size);
    packet.size=size;
    packet.queuedAt=millis();
    const bool accepted=xQueueSend(packets_,&packet,0)==pdTRUE;
    if (size==SECURE_RADIO_SIZE) {
      if (accepted) ++secure_.enqueued;
      else ++secure_.queueFull;
    }
    return accepted;
  }
  void completed(esp_now_send_status_t status) {
    int pending=0;
    if (!completion_.compare_exchange_strong(pending,status==ESP_NOW_SEND_SUCCESS?1:2))
      ++unexpected_;
  }
  bool stalled() const { return stalled_; }
  uint32_t unexpected() const { return unexpected_.load(); }
  const SecureTxDiagnostics &secureDiagnostics() const { return secure_; }
  void tick() {
    const int status=completion_.exchange(0);
    if (status) {
      if (!inflight_) ++unexpected_;
      else {
        if (inflightSecure_) {
          if (status==1) ++secure_.delivered;
          else ++secure_.deliveryFailed;
        }
        inflight_=false;
        stalled_=false;
#if RADIO_DEV_INSECURE
        Serial.printf("[DEV INSECURE] TX driver=%s\n",status==1?"OK":"FAIL");
#endif
      }
    }
    if (inflight_) {
      if (uint32_t(millis()-inflightAt_)>=3000) stalled_=true;
      return;
    }
    if (!packets_) return;
    DevTxPacket p;
    if (xQueueReceive(packets_,&p,0)!=pdTRUE) return;
    const bool secure=p.size==SECURE_RADIO_SIZE;
    if (uint32_t(millis()-p.queuedAt)>=1000) {
      if (secure) ++secure_.expired;
      return;
    }
    if (!esp_now_is_peer_exist(p.mac)) {
      if (secure) ++secure_.missingPeer;
      if (!allowDevPeer_ || (added_ && memcmp(addedMac_,p.mac,6))) return;
      esp_now_peer_info_t peer{};
      memcpy(peer.peer_addr,p.mac,6);
      peer.ifidx=WIFI_IF_STA;
      if (esp_now_add_peer(&peer)!=ESP_OK) return;
      added_=true;
      memcpy(addedMac_,p.mac,6);
    }
    const esp_err_t result=esp_now_send(p.mac,p.bytes,p.size);
    inflight_=result==ESP_OK;
    if (secure) {
      if (inflight_) ++secure_.submitted;
      else {
        ++secure_.apiRejected;
        secure_.lastApiError=result;
      }
    }
    inflightSecure_=inflight_ && secure;
    if (inflight_) inflightAt_=millis();
  }
 private:
  QueueHandle_t packets_=nullptr;
  std::atomic<int> completion_{0};
  std::atomic<uint32_t> unexpected_{0};
  SecureTxDiagnostics secure_;
  bool inflightSecure_=false;
  bool inflight_=false,stalled_=false,allowDevPeer_=false,added_=false;
  uint8_t addedMac_[6]{};
  uint32_t inflightAt_=0;
};
