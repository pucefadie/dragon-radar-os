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

  bool requestMotor(uint32_t now, uint8_t id=0) {
    auto *peer=findPeer(id);
    return peer && aligned_ && !tx_.stalled() && peer->requestMotor(now);
  }
  bool requestRgb(uint32_t now,uint8_t id,uint32_t command) {
    auto *peer=findPeer(id);
    return peer && aligned_ && !tx_.stalled() && peer->requestRgb(now,command);
  }
  SecureMotorStatus rgbStatus(uint8_t id) const {
    const auto *peer=findPeer(id);
    return peer ? peer->rgbStatus() : SecureMotorStatus::Unavailable;
  }
  void setRgbHandler(SecureRgbHandler handler) {
    if (auto *peer=findPeer(0)) peer->setRgbHandler(handler);
  }
  void cancelMotorRequest() {
    for (auto *peer : radio_) if (peer) peer->cancelMotorRequest();
  }
  SecureMotorStatus motorStatus(uint8_t id=0) const {
    auto *peer=findPeer(id);
    return peer ? peer->motorStatus() : SecureMotorStatus::Unavailable;
  }
  void setMotorHandler(SecureMotorHandler handler) {
    if (auto *peer=findPeer(0)) peer->setMotorHandler(handler);
  }
  uint8_t configuredMask() const { return configured_; }

  bool begin(bool controller, bool (*prepareChannel)(uint8_t) = nullptr, uint8_t localId=1) {
    if (configured_) return true;
    if (localId<1 || localId>SECURE_SPHERE_COUNT ||
        esp_wifi_get_mac(WIFI_IF_STA,local_)!=ESP_OK) return false;
    nvs_handle_t storage;
    if (nvs_open(SECURE_RECORD_NAMESPACE,NVS_READONLY,&storage)!=ESP_OK) return false;
    channel_=0;
    controller_=controller;
    localId_=localId;
    if (!queue_) queue_=xQueueCreate(kQueueDepth,sizeof(Packet));
    uint8_t domainPmk[16]{};
    const uint8_t first=controller ? 1 : localId;
    const uint8_t last=controller ? kPeerCapacity : localId;
    for (uint8_t id=first; queue_ && id<=last; ++id) {
      SecureRadioConfig config{};
      if (secureRadioLoadConfig(readRecord,&storage,config,id) && secureRadioResolvePmk(config,*this))
        installPeer(config,domainPmk,prepareChannel);
      secureRadioClear(&config,sizeof(config));
    }
    nvs_close(storage);
    secureRadioClear(domainPmk,sizeof(domainPmk));
    if (!configured_) return false;

    // On IEEE 802.15.4 targets the eFuse API returns an EUI-64.
    uint8_t factory[8]{};
    const bool factoryRead=esp_efuse_mac_get_default(factory)==ESP_OK;
#if SOC_IEEE802154_SUPPORTED
    memmove(factory+3,factory+5,3);
#endif
    factorySta_=factoryRead && !memcmp(factory,local_,6);
    accepting_.store(true,std::memory_order_release);
    return true;
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
#if CONFIG_IDF_TARGET_ESP32S3
    p.rssi=info->rx_ctrl ? info->rx_ctrl->rssi : 0;
#endif
    p.received=millis();
    if (xQueueSend(queue_,&p,0)!=pdTRUE) ++drop_;
  }
  void tick(uint32_t now
#if CONFIG_IDF_TARGET_ESP32S3
            , void (*observe)(uint8_t, SecureRxEvent, int8_t, uint32_t)=nullptr
#endif
  ) {
    if (!configured_) return;
    uint8_t primary=0; wifi_second_chan_t secondary{};
    aligned_=esp_wifi_get_channel(&primary,&secondary)==ESP_OK && primary==channel_;
    Packet p;
    for (unsigned n=0;n<kQueueDepth && xQueueReceive(queue_,&p,0)==pdTRUE;++n) {
      const uint8_t id=controller_ ? p.bytes[5] : localId_;
      if (aligned_ && id>=1 && id<=SECURE_SPHERE_COUNT) {
        auto *peer=findPeer(id);
        if (!peer) continue;
        ++rxProcessed_;
        const uint32_t receivedAge=millis()-p.received;
#if CONFIG_IDF_TARGET_ESP32S3
        const auto event=peer->receive(p.source,p.bytes,sizeof(p.bytes),now,receivedAge);
        if (observe && event!=SecureRxEvent::None) observe(id,event,p.rssi,p.received);
#else
        peer->receive(p.source,p.bytes,sizeof(p.bytes),now,receivedAge);
#endif
      }
    }
    if (aligned_) {
      for (auto *peer : radio_) if (peer) peer->tick(now);
    } else cancelMotorRequest();
  }
  SecureLinkStatus linkStatus(uint32_t now, uint8_t id=0) const {
    auto *peer=findPeer(id);
    return peer ? peer->linkStatus(now, aligned_) : SecureLinkStatus::Unavailable;
  }
  void print(Stream &out,uint32_t now,uint8_t id=0) const {
    if (!id) id=localId_;
    auto *peer=findPeer(id);
    const SecureRadioDiagnostics diagnostics=peer?peer->diagnostics():SecureRadioDiagnostics{};
    out.printf("[SECURE RADIO] configured=%d aligned=%d state=%u confirmed=%lu rx_drop=%lu tx_stalled=%d"
      " probe_retry_queued=%lu auth_rejected=%lu stale_session=%lu duplicate_probe=%lu sphere_id=%u\n",
      peer!=nullptr,aligned_,peer?unsigned(peer->state(now)):0,
      peer?(unsigned long)peer->confirmations():0,(unsigned long)drop_.load(),tx_.stalled(),
      (unsigned long)diagnostics.probe_retry_queued,(unsigned long)diagnostics.auth_rejected,
      (unsigned long)diagnostics.stale_session,(unsigned long)diagnostics.duplicate_probe,unsigned(id));
    const SecureTxDiagnostics tx=tx_.secureDiagnostics();
    out.printf("[SECURE TRANSPORT] sta_factory=%d record_version=%u pmk_result=%d peer_result=%d"
      " sign_errors=%lu rx_callbacks=%lu rx_unavailable=%lu"
      " rx_wrong_destination=%lu rx_processed=%lu tx_enqueued=%lu tx_queue_full=%lu tx_expired=%lu"
      " tx_missing_peer=%lu tx_submitted=%lu tx_api_rejected=%lu tx_delivered=%lu tx_delivery_failed=%lu last_tx_error=%d\n",
      factorySta_,peer?unsigned(versions_[slotIndex(id)]):0,int(pmkResult_),int(peer?ESP_OK:ESP_ERR_NOT_FOUND),
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
    auto *peer=findPeer(0);
    return peer && aligned_ && peer->benchAction(action,now);
  }
  void printBench(Stream &out) const {
    auto *peer=findPeer(0);
    const M1cRadioResult result=peer?peer->benchResult():M1cRadioResult{};
    out.printf("[M1C FIXTURE] ack_dropped=%lu corrupt_queued=%lu captured=%d replay_queued=%lu\n",
      (unsigned long)result.ack_dropped,(unsigned long)result.corrupt_queued,
      result.captured,(unsigned long)result.replay_queued);
  }
#endif
 private:
#if CONFIG_IDF_TARGET_ESP32S3
  static constexpr unsigned kPeerCapacity=SECURE_SPHERE_COUNT, kQueueDepth=24;
#else
  static constexpr unsigned kPeerCapacity=1, kQueueDepth=8;
#endif
  unsigned slotIndex(uint8_t id) const { return controller_ ? id-1 : 0; }
  SecureRadio *findPeer(uint8_t id) const {
    if (!id) id=localId_;
    if (id>SECURE_SPHERE_COUNT || (!controller_ && id!=localId_)) return nullptr;
    const unsigned index=slotIndex(id);
    return index<kPeerCapacity ? radio_[index] : nullptr;
  }
  void installPeer(const SecureRadioConfig &config,uint8_t *domainPmk,bool (*prepareChannel)(uint8_t)) {
    if (!memcmp(config.peer,local_,6) || esp_now_is_peer_exist(config.peer)) return;
    if (!channel_) {
      if (prepareChannel && !prepareChannel(config.channel)) return;
      pmkResult_=esp_now_set_pmk(config.pmk);
      if (pmkResult_!=ESP_OK) return;
      channel_=config.channel;
      memcpy(domainPmk,config.pmk,sizeof(config.pmk));
    }
    if (channel_!=config.channel || memcmp(domainPmk,config.pmk,sizeof(config.pmk))) return;
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr,config.peer,6);
    memcpy(peer.lmk,config.lmk,sizeof(peer.lmk));
    peer.ifidx=WIFI_IF_STA;
    peer.channel=config.channel;
    peer.encrypt=true;
    const bool installed=esp_now_add_peer(&peer)==ESP_OK;
    secureRadioClear(&peer,sizeof(peer));
    if (!installed) return;
    const unsigned index=slotIndex(config.id);
    radio_[index]=new (storage_[index]) SecureRadio(*this,controller_,config);
    versions_[index]=config.version;
    configured_|=uint8_t(1u<<(config.id-1));
  }
  struct Packet {
    uint8_t source[6],bytes[SECURE_RADIO_SIZE];
#if CONFIG_IDF_TARGET_ESP32S3
    int8_t rssi;
#endif
    uint32_t received;
  };
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
  bool sendMotor(const uint8_t *mac,const uint8_t *data,size_t size) override {
    return aligned_ && tx_.enqueue(mac,data,size,300);
  }
  DevRadioTx &tx_;
  alignas(SecureRadio) uint8_t storage_[kPeerCapacity][sizeof(SecureRadio)]{};
  SecureRadio *radio_[kPeerCapacity]{};
  SecureRecordVersion versions_[kPeerCapacity]{};
  QueueHandle_t queue_=nullptr;
  uint8_t local_[6]{},channel_=0,configured_=0,localId_=1;
  esp_err_t pmkResult_=ESP_ERR_INVALID_STATE;
  bool aligned_=false,factorySta_=false,controller_=false;
  uint32_t signErrors_=0,rxProcessed_=0;
  std::atomic<uint32_t> drop_{0};
  std::atomic<uint32_t> rxCallbacks_{0},rxUnavailable_{0},rxWrongDestination_{0};
  std::atomic<bool> accepting_{false};
};
