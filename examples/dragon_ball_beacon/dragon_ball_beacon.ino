// Experimental Dragon Radar beacon. Broadcast frames are NOT authenticated.
// Configure a unique ID (1..7) and the radar's current Wi-Fi channel.
// No pins, battery divider, sleep or ranging hardware assumed.
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_system.h>
#include "beacon_protocol.h"
#include "beacon_config.h"
#include "dev_radio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#ifndef RADIO_DEV_INSECURE
#define RADIO_DEV_INSECURE 0
#endif
#if RADIO_DEV_INSECURE
struct DevPacket { uint8_t mac[6], bytes[RADIO_FRAME_SIZE]; };
static QueueHandle_t devQueue;
class DevAdapter : public DevRadioTransport { public: bool send(const uint8_t *mac,const uint8_t *data) override {
 if(!esp_now_is_peer_exist(mac)){esp_now_peer_info_t peer={};memcpy(peer.peer_addr,mac,6);peer.ifidx=WIFI_IF_STA;if(esp_now_add_peer(&peer)!=ESP_OK)return false;}
 esp_err_t e=esp_now_send(mac,data,RADIO_FRAME_SIZE); Serial.printf("[DEV INSECURE] local enqueue=%d\n",int(e));return e==ESP_OK;
}};
static DevAdapter devAdapter; static DevSphere *devSphere;
static void devReceive(const esp_now_recv_info_t *info,const uint8_t *data,int len) { if (!devQueue || !info || !info->src_addr || !data || len!=RADIO_FRAME_SIZE) return; DevPacket p; memcpy(p.mac,info->src_addr,6); memcpy(p.bytes,data,RADIO_FRAME_SIZE); xQueueSend(devQueue,&p,0); }
#endif
#ifndef DRAGON_BALL_ID
#define DRAGON_BALL_ID 1
#endif
#ifndef DRAGON_RADIO_CHANNEL
#define DRAGON_RADIO_CHANNEL 1
#endif
#if DRAGON_BALL_ID < 1 || DRAGON_BALL_ID > 7
#error DRAGON_BALL_ID must be between 1 and 7
#endif
#if DRAGON_RADIO_CHANNEL < 1 || DRAGON_RADIO_CHANNEL > 13
#error DRAGON_RADIO_CHANNEL must be between 1 and 13 and legal for your region
#endif
static const uint8_t broadcastMac[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
static bool ready=false;
static uint32_t bootId=0, sequence=0, nextBeacon=0;
void setup() {
  Serial.begin(115200);
#if defined(ARDUINO_XIAO_ESP32C6)
  // Seeed XIAO ESP32-C6: enable RF switch, then select external U.FL antenna.
  // Use only with the antenna connected. GPIO3/14 are dedicated to this switch.
  pinMode(WIFI_ENABLE,OUTPUT); digitalWrite(WIFI_ENABLE,LOW);
  delay(100);
  pinMode(WIFI_ANT_CONFIG,OUTPUT); digitalWrite(WIFI_ANT_CONFIG,HIGH);
#endif
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  if (esp_wifi_set_channel(DRAGON_RADIO_CHANNEL,WIFI_SECOND_CHAN_NONE)!=ESP_OK || esp_now_init()!=ESP_OK) {
    Serial.println("ESP-NOW init failed"); return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr,broadcastMac,6);
  peer.channel=DRAGON_RADIO_CHANNEL; peer.ifidx=WIFI_IF_STA; peer.encrypt=false;
  if (esp_now_add_peer(&peer)!=ESP_OK) { Serial.println("ESP-NOW peer failed"); return; }
  bootId=esp_random(); ready=true;
#if RADIO_DEV_INSECURE
  static DevSphere sphere(devAdapter,DRAGON_BALL_ID,bootId); devSphere=&sphere;
  devQueue=xQueueCreate(8,sizeof(DevPacket));
  if (!devQueue || esp_now_register_recv_cb(devReceive)!=ESP_OK) { Serial.println("DEV RX init failed"); ready=false; return; }
  Serial.println("[DEV INSECURE] unencrypted, unauthenticated; actuators disabled");
#endif
  Serial.printf("Dragon beacon ID=%d channel=%d (broadcast, not authenticated)\n",DRAGON_BALL_ID,DRAGON_RADIO_CHANNEL);
}
void loop() {
  if (!ready) { delay(100); return; }
  #if RADIO_DEV_INSECURE
  devSphere->tick(millis());
  DevPacket packet; unsigned budget=8;
  while (budget-- && xQueueReceive(devQueue,&packet,0)==pdTRUE) devSphere->receive(packet.mac,packet.bytes,sizeof(packet.bytes),millis());
#endif
  if (int32_t(millis()-nextBeacon)>=0) {
    nextBeacon=millis()+470+(esp_random()%61); // ~500 ms, +/-30 ms jitter
    BeaconFrame frame={DRAGON_BALL_ID,false,bootId,sequence++,0};
    uint8_t bytes[BEACON_FRAME_SIZE]; beaconEncode(frame,bytes);
    esp_err_t result=esp_now_send(broadcastMac,bytes,sizeof(bytes));
    if (result!=ESP_OK) Serial.printf("send error: %d\n",(int)result);
  }
  delay(10);
}
