#include "beacon_service.h"
#include "beacon_protocol.h"
#include "beacon_registry.h"
#include "dev_radio.h"
#if RADIO_DEV_INSECURE
#include "dev_radio_tx.h"
#endif
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
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
#endif
std::atomic<uint32_t> dropped{0}; uint32_t malformed=0, conflicts=0, duplicates=0;
void receive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  if (!queue || !info || !info->src_addr || !data || length != (int)BEACON_FRAME_SIZE) return;
  Packet packet; memcpy(packet.mac,info->src_addr,6); memcpy(packet.bytes,data,BEACON_FRAME_SIZE);
  packet.rssi=info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  if (xQueueSend(queue,&packet,0)!=pdTRUE) ++dropped;
}
}
void beaconServiceInit() {
  if (ready) return;
  if (!queue) queue=xQueueCreate(16,sizeof(Packet));
  if (!queue) return;
  if (esp_now_init()!=ESP_OK) return;
  if (esp_now_register_recv_cb(receive)!=ESP_OK) { esp_now_deinit(); return; }
  ready=true;
#if RADIO_DEV_INSECURE
  if(!radioTx.init() || esp_now_register_send_cb(txDone)!=ESP_OK) { ready=false; esp_now_deinit(); return; }
  static DevRadar radar(devAdapter,esp_random()|1); devRadar=&radar; Serial.println("[DEV INSECURE] unencrypted, unauthenticated; actuators disabled");
#endif
}
void beaconServiceUpdate() {
  static uint32_t lastRetry=0;
  if (!ready && uint32_t(millis()-lastRetry)>=5000) { lastRetry=millis(); beaconServiceInit(); }
  if (!ready || !queue) return;
  Packet p; unsigned budget=16;
  while (budget-- && xQueueReceive(queue,&p,0)==pdTRUE) {
    RadioFrame rf{}; if (!radioDecode(p.bytes,sizeof(p.bytes),rf)) { ++malformed; continue; }
#if RADIO_DEV_INSECURE
    if (rf.type!=RadioMessage::Beacon) { Serial.printf("[DEV INSECURE] RX type=%s id=%u seq=%lu RSSI=%d channel=%u\n",devMessageName(rf.type),rf.id,(unsigned long)rf.sequence,p.rssi,beaconServiceChannel()); devRadar->receive(p.mac,p.bytes,sizeof(p.bytes),millis()); continue; }
#endif
    BeaconFrame frame; if (!beaconDecode(p.bytes,sizeof(p.bytes),frame)) { ++malformed; continue; }
    BeaconAccept accepted=registry.accept(frame,p.mac,p.rssi,millis());
    #if RADIO_DEV_INSECURE
    if (accepted==BeaconAccept::Accepted) { static uint32_t lastBeaconLog=0; if(uint32_t(millis()-lastBeaconLog)>=5000){lastBeaconLog=millis();Serial.printf("[DEV INSECURE] BEACON id=%u seq=%lu RSSI=%d channel=%u\n",frame.id,(unsigned long)frame.sequence,p.rssi,beaconServiceChannel());} devRadar->beacon(p.mac,frame.id,frame.boot_id,millis()); }
#endif
    if (accepted==BeaconAccept::Conflict) ++conflicts;
    else if (accepted==BeaconAccept::Duplicate) ++duplicates;
    else if (accepted==BeaconAccept::Invalid) ++malformed;
  }
#if RADIO_DEV_INSECURE
  devRadar->tick(millis());
  radioTx.tick();
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
  out.printf("[BEACON] %s channel=%u channel_ok=%d drop=%lu invalid=%lu conflict=%lu duplicate=%lu\n",ready?"RX ON":"RX OFF",primary,channelError==ESP_OK,(unsigned long)dropped.load(),(unsigned long)malformed,(unsigned long)conflicts,(unsigned long)duplicates);
  for (unsigned i=0;i<7;++i) {
    const BeaconEntry &e=*registry.entry(i); uint32_t age=uint32_t(millis()-e.received); bool fresh=BeaconRegistry::fresh(e,millis());
    if (!e.seen) { out.printf("  #%u: NON RILEVATA\n",i+1); continue; }
    out.printf("  #%u: %s signal=%d dBm age=%lu ms seq=%lu boot=%08lx battery=%u mV\n",i+1,fresh?"RICEVUTA NON AUTENTICATA":"SCADUTA",e.rssi,(unsigned long)age,(unsigned long)e.frame.sequence,(unsigned long)e.frame.boot_id,e.frame.battery_valid?e.frame.battery_mv:0);
  }
  out.println("[BEACON] RSSI non fornisce distanza o direzione; nessuna localizzazione reale.");
}
