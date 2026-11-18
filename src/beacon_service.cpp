#include "beacon_service.h"
#include "beacon_protocol.h"
#include "beacon_registry.h"
#include "dev_radio.h"
#if RADIO_DEV_INSECURE
#include "dev_radio_tx.h"
#elif !defined(RADIO_RX_HOST_TEST)
#include "secure_radio_espnow.h"
#endif
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <WiFi.h>
#ifndef RADIO_DEV_INSECURE
#define RADIO_DEV_INSECURE 0
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <string.h>
namespace {
struct Packet { uint8_t mac[6]; uint8_t bytes[BEACON_FRAME_SIZE]; int8_t rssi; };
BeaconRegistry registry; QueueHandle_t queue=nullptr; bool ready=false;
#if RADIO_DEV_INSECURE
DevRadioTx radioTx;
void txDone(const esp_now_send_info_t *,esp_now_send_status_t status) { radioTx.completed(status); }
class DevAdapter : public DevRadioTransport { public: bool send(const uint8_t *mac,const uint8_t *data) override {
 bool ok=radioTx.enqueue(mac,data); RadioFrame f{};radioDecode(data,RADIO_FRAME_SIZE,f); Serial.printf("[DEV INSECURE] queued=%d type=%s id=%u seq=%lu channel=%u\n",ok,devMessageName(f.type),f.destination,(unsigned long)f.sequence,beaconServiceChannel());return ok;
 } void event(DevEvent e,uint8_t id,uint32_t seq,uint32_t value) override {const char *names[]={"TX_BUSY_TIMEOUT","TIMEOUT","LINK_LOST","LINK_RECOVERED","RESPONSE"};Serial.printf("[DEV INSECURE] %s id=%u seq=%lu RTT=%lu ms channel=%u\n",names[unsigned(e)],id,(unsigned long)seq,(unsigned long)value,beaconServiceChannel());}
};
DevAdapter devAdapter; DevRadar *devRadar=nullptr;
#elif !defined(RADIO_RX_HOST_TEST)
DevRadioTx secureTx;
SecureEspNow secure(secureTx);
bool secureReady=false;
void txDone(const esp_now_send_info_t *,esp_now_send_status_t status) { secureTx.completed(status); }
#endif
std::atomic<uint32_t> dropped{0}, callbacks{0}, badMetadata{0}, len34{0}, lenOther{0}, queued{0};
uint32_t malformed=0, conflicts=0, duplicates=0, delivered=0, magicOk=0, magicBad=0, protocolOk=0, protocolBad=0, crcOk=0, crcBad=0, beaconType=0, otherType=0, idOk=0, idBad=0, decoded=0, acceptedCount=0, rejected=0, rejectInvalid=0;
esp_err_t initResult=ESP_ERR_INVALID_STATE, rxRegisterResult=ESP_ERR_INVALID_STATE, txRegisterResult=ESP_ERR_INVALID_STATE;
uint32_t initAttempts=0, modeChanges=0; wifi_mode_t initialMode=WIFI_MODE_NULL, lastMode=WIFI_MODE_NULL; bool modeKnown=false;
std::atomic<uint32_t> connectedEvents{0}, disconnectedEvents{0};
void wifiEvent(WiFiEvent_t event) {
  if (event==ARDUINO_EVENT_WIFI_STA_CONNECTED) ++connectedEvents;
  else if (event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED) ++disconnectedEvents;
}
void receive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  ++callbacks;
#if !RADIO_DEV_INSECURE && !defined(RADIO_RX_HOST_TEST)
  if (length==SECURE_RADIO_SIZE) secure.enqueue(info,data,length);
#endif
  if (length == (int)BEACON_FRAME_SIZE) ++len34; else ++lenOther;
  if (!info || !info->src_addr || !info->rx_ctrl || !data) { ++badMetadata; return; }
  if (!queue || length != (int)BEACON_FRAME_SIZE) return;
  Packet packet; memcpy(packet.mac,info->src_addr,6); memcpy(packet.bytes,data,BEACON_FRAME_SIZE);
  packet.rssi=info->rx_ctrl->rssi;
  if (xQueueSend(queue,&packet,0)!=pdTRUE) ++dropped; else ++queued;
}
}
void beaconServiceInit() {
  if (ready) return;
  static bool eventsRegistered=false;
  if (!eventsRegistered) { WiFi.onEvent(wifiEvent); eventsRegistered=true; }
  ++initAttempts;
  wifi_mode_t mode; if (esp_wifi_get_mode(&mode)==ESP_OK && !modeKnown) { initialMode=lastMode=mode; modeKnown=true; }
  if (!queue) queue=xQueueCreate(16,sizeof(Packet));
  if (!queue) return;
  initResult=esp_now_init(); if (initResult!=ESP_OK) return;
  rxRegisterResult=esp_now_register_recv_cb(receive); if (rxRegisterResult!=ESP_OK) { esp_now_deinit(); return; }
  ready=true;
#if !RADIO_DEV_INSECURE && !defined(RADIO_RX_HOST_TEST)
  if (secureTx.init(false)) {
    txRegisterResult=esp_now_register_send_cb(txDone);
    secureReady=txRegisterResult==ESP_OK && secure.begin(true);
  }
#endif
#if RADIO_DEV_INSECURE
  if(!radioTx.init()) { ready=false; esp_now_deinit(); return; }
  txRegisterResult=esp_now_register_send_cb(txDone); if(txRegisterResult!=ESP_OK) { ready=false; esp_now_deinit(); return; }
  static DevRadar radar(devAdapter,esp_random()|1); devRadar=&radar; Serial.println("[DEV INSECURE] unencrypted, unauthenticated; actuators disabled");
#endif
}
void beaconServiceUpdate() {
  wifi_mode_t currentMode; if (esp_wifi_get_mode(&currentMode)==ESP_OK && modeKnown && currentMode!=lastMode) { ++modeChanges; lastMode=currentMode; }
  static uint32_t lastRetry=0;
  if (!ready && uint32_t(millis()-lastRetry)>=5000) { lastRetry=millis(); beaconServiceInit(); }
  if (!ready || !queue) return;
  Packet p; unsigned budget=16;
  while (budget-- && xQueueReceive(queue,&p,0)==pdTRUE) {
    ++delivered;
    const uint8_t *b=p.bytes;
    if (!memcmp(b,"DRB2",4)) ++magicOk; else ++magicBad;
    if (b[4]==RADIO_PROTOCOL_VERSION) ++protocolOk; else ++protocolBad;
    if (uint16_t(b[32]|uint16_t(b[33])<<8)==radioCrc16(b,32)) ++crcOk; else ++crcBad;
    if (b[5]==uint8_t(RadioMessage::Beacon)) ++beaconType; else ++otherType;
    if (b[6]>=1 && b[6]<=7) ++idOk; else ++idBad;
    RadioFrame rf{}; if (!radioDecode(p.bytes,sizeof(p.bytes),rf)) { ++malformed; continue; }
#if RADIO_DEV_INSECURE
    if (rf.type!=RadioMessage::Beacon) { Serial.printf("[DEV INSECURE] RX type=%s id=%u seq=%lu RSSI=%d channel=%u\n",devMessageName(rf.type),rf.id,(unsigned long)rf.sequence,p.rssi,beaconServiceChannel()); devRadar->receive(p.mac,p.bytes,sizeof(p.bytes),millis()); continue; }
#endif
    BeaconFrame frame; if (!beaconDecode(p.bytes,sizeof(p.bytes),frame)) { ++malformed; continue; }
    ++decoded;
    BeaconAccept accepted=registry.accept(frame,p.mac,p.rssi,millis());
    #if RADIO_DEV_INSECURE
    if (accepted==BeaconAccept::Accepted) { static uint32_t lastBeaconLog=0; if(uint32_t(millis()-lastBeaconLog)>=5000){lastBeaconLog=millis();Serial.printf("[DEV INSECURE] BEACON id=%u seq=%lu RSSI=%d channel=%u\n",frame.id,(unsigned long)frame.sequence,p.rssi,beaconServiceChannel());} devRadar->beacon(p.mac,frame.id,frame.boot_id,millis()); }
#endif
    if (accepted==BeaconAccept::Accepted) ++acceptedCount;
    else { ++rejected; if (accepted==BeaconAccept::Invalid) ++rejectInvalid; }
    if (accepted==BeaconAccept::Conflict) ++conflicts;
    else if (accepted==BeaconAccept::Duplicate) ++duplicates;
  }
#if RADIO_DEV_INSECURE
  devRadar->tick(millis());
  radioTx.tick();
#elif !defined(RADIO_RX_HOST_TEST)
  if (secureReady) { secure.tick(millis()); secureTx.tick(); }
#endif
}
uint8_t beaconServiceChannel() {
  uint8_t channel=0; wifi_second_chan_t secondary=WIFI_SECOND_CHAN_NONE;
  return esp_wifi_get_channel(&channel,&secondary)==ESP_OK ? channel : 0;
}
uint8_t beaconServiceFreshCount() {
  uint8_t count=0; const uint32_t now=millis();
  for (unsigned i=0;i<7;++i) if (BeaconRegistry::fresh(*registry.entry(i),now)) ++count;
  return count;
}
void beaconServicePrint(Stream &out) {
  uint8_t primary=0; wifi_second_chan_t secondary=WIFI_SECOND_CHAN_NONE;
  esp_err_t channelError=esp_wifi_get_channel(&primary,&secondary);
  out.printf("[BEACON] %s channel=%u channel_ok=%d drop=%lu invalid=%lu conflict=%lu duplicate=%lu\n",ready?"RX ON":"RX OFF",primary,channelError==ESP_OK,(unsigned long)dropped.load(),(unsigned long)(malformed+rejectInvalid),(unsigned long)conflicts,(unsigned long)duplicates);
  wifi_mode_t mode=WIFI_MODE_NULL; esp_err_t modeError=esp_wifi_get_mode(&mode);
  wifi_ap_record_t ap{}; esp_err_t apError=esp_wifi_sta_get_ap_info(&ap);
  out.println("RADIO RX DIAGNOSTICS (counters since boot; queued frames classified independently)");
  out.printf("RX_CALLBACK_TOTAL=%lu RX_BAD_METADATA=%lu RX_LEN_34=%lu RX_LEN_OTHER=%lu RX_QUEUE_OK=%lu RX_QUEUE_DROP=%lu RX_DECODER_DELIVERED=%lu\n",(unsigned long)callbacks.load(),(unsigned long)badMetadata.load(),(unsigned long)len34.load(),(unsigned long)lenOther.load(),(unsigned long)queued.load(),(unsigned long)dropped.load(),(unsigned long)delivered);
  out.printf("RX_MAGIC_OK=%lu RX_MAGIC_BAD=%lu RX_PROTOCOL_OK=%lu RX_PROTOCOL_BAD=%lu RX_CRC_OK=%lu RX_CRC_BAD=%lu RX_TYPE_BEACON=%lu RX_TYPE_OTHER=%lu RX_ID_OK=%lu RX_ID_BAD=%lu\n",(unsigned long)magicOk,(unsigned long)magicBad,(unsigned long)protocolOk,(unsigned long)protocolBad,(unsigned long)crcOk,(unsigned long)crcBad,(unsigned long)beaconType,(unsigned long)otherType,(unsigned long)idOk,(unsigned long)idBad);
  out.printf("RX_BEACON_DECODED=%lu RX_REGISTRY_ACCEPTED=%lu RX_REGISTRY_REJECTED=%lu RX_REJECT_INVALID=%lu RX_REJECT_CONFLICT=%lu RX_REJECT_DUPLICATE=%lu RX_DECODE_INVALID=%lu\n",(unsigned long)decoded,(unsigned long)acceptedCount,(unsigned long)rejected,(unsigned long)rejectInvalid,(unsigned long)conflicts,(unsigned long)duplicates,(unsigned long)malformed);
  out.printf("WIFI_CHANNEL=%u CHANNEL_ERR=%d MODE=%d MODE_ERR=%d STA_ENABLED=%d AP_CONNECTED=%d AP_INFO_ERR=%d STA_CONNECTED_EVENTS=%lu STA_DISCONNECTED_EVENTS=%lu INITIAL_MODE=%d MODE_CHANGES_OBSERVED=%lu\n",primary,int(channelError),int(mode),int(modeError),mode==WIFI_MODE_STA||mode==WIFI_MODE_APSTA,apError==ESP_OK,int(apError),(unsigned long)connectedEvents.load(),(unsigned long)disconnectedEvents.load(),int(initialMode),(unsigned long)modeChanges);
  out.printf("ESPNOW_READY=%d INIT_ATTEMPTS=%lu INIT_RESULT=%d RX_REGISTER_RESULT=%d TX_REGISTER_RESULT=%d CALLBACK_OBSERVED=%d\n",ready,(unsigned long)initAttempts,int(initResult),int(rxRegisterResult),int(txRegisterResult),callbacks.load()!=0);
  for (unsigned i=0;i<7;++i) {
    const BeaconEntry &e=*registry.entry(i); uint32_t age=uint32_t(millis()-e.received); bool fresh=BeaconRegistry::fresh(e,millis());
    if (!e.seen) { out.printf("  #%u: NON RILEVATA\n",i+1); continue; }
    out.printf("  #%u: %s signal=%d dBm age=%lu ms seq=%lu boot=%08lx battery=%u mV\n",i+1,fresh?"RICEVUTA NON AUTENTICATA":"SCADUTA",e.rssi,(unsigned long)age,(unsigned long)e.frame.sequence,(unsigned long)e.frame.boot_id,e.frame.battery_valid?e.frame.battery_mv:0);
  }
#if !RADIO_DEV_INSECURE && !defined(RADIO_RX_HOST_TEST)
  secure.print(out,millis());
#endif
  out.println("[BEACON] RSSI non fornisce distanza o direzione; nessuna localizzazione reale.");
}
