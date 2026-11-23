#pragma once
#include <nvs.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <new>
#include "dev_radio_tx.h"

#if M1C_LINK_PMK_PROBE
#error "The M1c PMK experiment has been retired. Select a provisioned record instead."
#endif

class SecureEspNow final : private SecureRadioIo {
 public:
  explicit SecureEspNow(DevRadioTx &tx) : tx_(tx) {}

  // Only a deliberately provisioned one-sphere link may register an encrypted peer.
  bool begin(bool controller) {
    if (radio_) return true;
    nvs_handle_t storage;
    SecureRadioConfig config{};
    if (nvs_open(SECURE_RECORD_NAMESPACE,NVS_READONLY,&storage)!=ESP_OK) return false;
    const bool loaded=secureRadioLoadConfig(readRecord,&storage,config);
    nvs_close(storage);
    if (!loaded || !secureRadioResolvePmk(config,*this)) {
      secureRadioClear(&config,sizeof(config));
      return false;
    }
    if (!queue_) queue_=xQueueCreate(8,sizeof(Packet));
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr,config.peer,6);
    memcpy(peer.lmk,config.lmk,sizeof(peer.lmk));
    peer.ifidx=WIFI_IF_STA;
    peer.channel=config.channel; // Let the driver reject TX if the channel moves.
    peer.encrypt=true;
    // Never replace an existing peer or fall back to plaintext.
    if (queue_ && esp_wifi_get_mac(WIFI_IF_STA,local_)==ESP_OK &&
        memcmp(config.peer,local_,6) && !esp_now_is_peer_exist(config.peer)) {
      pmkResult_=esp_now_set_pmk(config.pmk);
      if (pmkResult_==ESP_OK) peerResult_=esp_now_add_peer(&peer);
    }
    const bool installed=pmkResult_==ESP_OK && peerResult_==ESP_OK;
    if (installed) {
      // On IEEE 802.15.4 targets the eFuse API returns an EUI-64.
      uint8_t factory[8]{};
      const bool factoryRead=esp_efuse_mac_get_default(factory)==ESP_OK;
#if SOC_IEEE802154_SUPPORTED
      memmove(factory+3,factory+5,3);
#endif
      factorySta_=factoryRead && !memcmp(factory,local_,6);
      radio_=new (storage_) SecureRadio(*this,controller,config);
      channel_=config.channel;
      recordVersion_=config.version;
      accepting_.store(true,std::memory_order_release);
    }
    secureRadioClear(&config,sizeof(config));
    secureRadioClear(&peer,sizeof(peer));
    return installed;
  }
  void enqueue(const esp_now_recv_info_t *info,const uint8_t *bytes,int length) {
    if (length!=SECURE_RADIO_SIZE) return;
    ++rxCallbacks_;
    if (!accepting_.load(std::memory_order_acquire) || !info || !info->src_addr || !info->des_addr || !bytes) {
      ++rxUnavailable_;
      return;
    }
    if (memcmp(info->des_addr,local_,6)) {
      ++rxWrongDestination_;
      return;
    }
    Packet p;
    memcpy(p.source,info->src_addr,6);
    memcpy(p.bytes,bytes,sizeof(p.bytes));
    if (xQueueSend(queue_,&p,0)!=pdTRUE) ++drop_;
  }
  void tick(uint32_t now) {
    if (!radio_) return;
    uint8_t primary=0; wifi_second_chan_t secondary{};
    aligned_=esp_wifi_get_channel(&primary,&secondary)==ESP_OK && primary==channel_;
    Packet p;
    for (unsigned n=0;n<8 && xQueueReceive(queue_,&p,0)==pdTRUE;++n) {
      if (aligned_) {
        ++rxProcessed_;
        radio_->receive(p.source,p.bytes,sizeof(p.bytes),now);
      }
    }
    if (aligned_) radio_->tick(now);
  }
  void print(Stream &out,uint32_t now) const {
    const SecureRadioDiagnostics diagnostics=radio_?radio_->diagnostics():SecureRadioDiagnostics{};
    out.printf("[SECURE RADIO] configured=%d aligned=%d state=%u confirmed=%lu rx_drop=%lu tx_stalled=%d"
      " probe_retry_queued=%lu auth_rejected=%lu stale_session=%lu duplicate_probe=%lu\n",
      radio_!=nullptr,aligned_,radio_?unsigned(radio_->state(now)):0,
      radio_?(unsigned long)radio_->confirmations():0,(unsigned long)drop_.load(),tx_.stalled(),
      (unsigned long)diagnostics.probe_retry_queued,(unsigned long)diagnostics.auth_rejected,
      (unsigned long)diagnostics.stale_session,(unsigned long)diagnostics.duplicate_probe);
    const SecureTxDiagnostics tx=tx_.secureDiagnostics();
    out.printf("[SECURE TRANSPORT] sta_factory=%d record_version=%u pmk_result=%d peer_result=%d"
      " sign_errors=%lu rx_callbacks=%lu rx_unavailable=%lu"
      " rx_wrong_destination=%lu rx_processed=%lu tx_enqueued=%lu tx_queue_full=%lu tx_expired=%lu"
      " tx_missing_peer=%lu tx_submitted=%lu tx_api_rejected=%lu tx_delivered=%lu tx_delivery_failed=%lu last_tx_error=%d\n",
      factorySta_,unsigned(recordVersion_),int(pmkResult_),int(peerResult_),
      (unsigned long)signErrors_,(unsigned long)rxCallbacks_.load(),(unsigned long)rxUnavailable_.load(),
      (unsigned long)rxWrongDestination_.load(),(unsigned long)rxProcessed_,
      (unsigned long)tx.enqueued,(unsigned long)tx.queueFull,(unsigned long)tx.expired,(unsigned long)tx.missingPeer,
      (unsigned long)tx.submitted,(unsigned long)tx.apiRejected,(unsigned long)tx.delivered,(unsigned long)tx.deliveryFailed,int(tx.lastApiError));
  }
#if M1C_BENCH_FIXTURE
  bool benchAction(M1cRadioAction action,uint32_t now) {
    uint8_t primary=0;
    wifi_second_chan_t secondary{};
    aligned_=esp_wifi_get_channel(&primary,&secondary)==ESP_OK && primary==channel_;
    return radio_ && aligned_ && radio_->benchAction(action,now);
  }
  void printBench(Stream &out) const {
    const M1cRadioResult result=radio_?radio_->benchResult():M1cRadioResult{};
    out.printf("[M1C FIXTURE] ack_dropped=%lu corrupt_queued=%lu captured=%d replay_queued=%lu\n",
      (unsigned long)result.ack_dropped,(unsigned long)result.corrupt_queued,
      result.captured,(unsigned long)result.replay_queued);
  }
#endif
 private:
  struct Packet { uint8_t source[6],bytes[SECURE_RADIO_SIZE]; };
  static SecureRecordRead readRecord(void *storage,const char *key,uint8_t *bytes,size_t &length) {
    const nvs_handle_t handle=*static_cast<nvs_handle_t*>(storage);
    nvs_type_t type;
    const esp_err_t found=nvs_find_key(handle,key,&type);
    if (found==ESP_ERR_NVS_NOT_FOUND) return SecureRecordRead::Missing;
    if (found!=ESP_OK || type!=NVS_TYPE_BLOB) return SecureRecordRead::Error;
    return nvs_get_blob(handle,key,bytes,&length)==ESP_OK?SecureRecordRead::Loaded:SecureRecordRead::Error;
  }
  bool sign(const uint8_t *key,const uint8_t *bytes,size_t size,uint8_t *tag) override {
    const auto *md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    const bool signedOk=md && mbedtls_md_hmac(md,key,32,bytes,size,tag)==0;
    if (!signedOk) ++signErrors_;
    return signedOk;
  }
  void random(uint8_t *bytes,size_t size) override { esp_fill_random(bytes,size); }
  bool send(const uint8_t *mac,const uint8_t *data,size_t size) override {
    return aligned_ && tx_.enqueue(mac,data,size);
  }
  DevRadioTx &tx_;
  alignas(SecureRadio) uint8_t storage_[sizeof(SecureRadio)]{};
  SecureRadio *radio_=nullptr;
  QueueHandle_t queue_=nullptr;
  uint8_t local_[6]{},channel_=0;
  SecureRecordVersion recordVersion_=SecureRecordVersion::None;
  esp_err_t pmkResult_=ESP_ERR_INVALID_STATE,peerResult_=ESP_ERR_INVALID_STATE;
  bool aligned_=false,factorySta_=false;
  uint32_t signErrors_=0,rxProcessed_=0;
  std::atomic<uint32_t> drop_{0};
  std::atomic<uint32_t> rxCallbacks_{0},rxUnavailable_{0},rxWrongDestination_{0};
  std::atomic<bool> accepting_{false};
};
