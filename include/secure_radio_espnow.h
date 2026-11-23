#pragma once
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <new>
#include "dev_radio_tx.h"

#if M1C_LINK_PMK_PROBE && (RADIO_DEV_INSECURE || (!M1C_BENCH_FIXTURE && !M1C_C6_NVS_GUARD))
#error "The shared PMK experiment is restricted to M1c bench radio builds."
#endif

class SecureEspNow final : private SecureRadioIo {
 public:
  explicit SecureEspNow(DevRadioTx &tx) : tx_(tx) {}

  // Only a deliberately provisioned one-sphere link may register an encrypted peer.
  bool begin(bool controller) {
    if (radio_) return true;
    Preferences prefs;
    uint8_t record[SECURE_CONFIG_SIZE]{};
    SecureRadioConfig config{};
    if (!prefs.begin("dragon_radio",true)) return false;
    const bool loaded=prefs.getBytesLength("peer_v1")==sizeof(record) &&
      prefs.getBytes("peer_v1",record,sizeof(record))==sizeof(record) &&
      secureRadioDecodeConfig(record,sizeof(record),config);
    prefs.end();
    erase(record,sizeof(record));
    if (!loaded) return false;
#if M1C_LINK_PMK_PROBE
    // A/B experiment for ESP-NOW interoperability. The on-air LMK and
    // application HMAC key stay the same. The stored records are untouched.
    static constexpr uint8_t context[]="DRA1 M1c bench shared PMK";
    uint8_t derived[32]{};
    const bool derivedOk=sign(config.auth,context,sizeof(context)-1,derived);
    if (derivedOk) memcpy(config.pmk,derived,sizeof(config.pmk));
    erase(derived,sizeof(derived));
    if (!derivedOk) {
      erase(&config,sizeof(config));
      return false;
    }
#endif
    if (!queue_) queue_=xQueueCreate(8,sizeof(Packet));
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr,config.peer,6);
    memcpy(peer.lmk,config.lmk,sizeof(peer.lmk));
    peer.ifidx=WIFI_IF_STA;
    peer.channel=config.channel; // Let the driver reject TX if the channel moves.
    peer.encrypt=true;
    // Never replace an existing peer or fall back to plaintext.
    const bool installed=queue_ && esp_wifi_get_mac(WIFI_IF_STA,local_)==ESP_OK &&
      memcmp(config.peer,local_,6) && !esp_now_is_peer_exist(config.peer) &&
      esp_now_set_pmk(config.pmk)==ESP_OK && esp_now_add_peer(&peer)==ESP_OK;
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
      accepting_.store(true,std::memory_order_release);
    }
    erase(&config,sizeof(config));
    erase(&peer,sizeof(peer));
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
    out.printf("[SECURE TRANSPORT] sta_factory=%d pmk_probe=%d sign_errors=%lu rx_callbacks=%lu rx_unavailable=%lu"
      " rx_wrong_destination=%lu rx_processed=%lu tx_enqueued=%lu tx_queue_full=%lu tx_expired=%lu"
      " tx_missing_peer=%lu tx_submitted=%lu tx_api_rejected=%lu tx_delivered=%lu tx_delivery_failed=%lu last_tx_error=%d\n",
      factorySta_,
#if M1C_LINK_PMK_PROBE
      1,
#else
      0,
#endif
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
  static void erase(void *p,size_t n) {
    volatile uint8_t *bytes=static_cast<volatile uint8_t*>(p);
    while(n--) *bytes++=0;
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
  bool aligned_=false,factorySta_=false;
  uint32_t signErrors_=0,rxProcessed_=0;
  std::atomic<uint32_t> drop_{0};
  std::atomic<uint32_t> rxCallbacks_{0},rxUnavailable_{0},rxWrongDestination_{0};
  std::atomic<bool> accepting_{false};
};
