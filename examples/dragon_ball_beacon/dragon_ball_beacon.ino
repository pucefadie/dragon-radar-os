// Experimental Dragon Radar beacon. Broadcast frames are NOT authenticated.
// Configure a unique ID (1..7) and the radar's current Wi-Fi channel.
// No pins, battery divider, sleep or ranging hardware assumed.
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_system.h>
#include "beacon_protocol.h"
#include "beacon_config.h"
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
  Serial.printf("Dragon beacon ID=%d channel=%d (broadcast, not authenticated)\n",DRAGON_BALL_ID,DRAGON_RADIO_CHANNEL);
}
void loop() {
  if (!ready) { delay(100); return; }
  if (int32_t(millis()-nextBeacon)>=0) {
    nextBeacon=millis()+470+(esp_random()%61); // ~500 ms, +/-30 ms jitter
    BeaconFrame frame={DRAGON_BALL_ID,false,bootId,sequence++,0};
    uint8_t bytes[BEACON_FRAME_SIZE]; beaconEncode(frame,bytes);
    esp_err_t result=esp_now_send(broadcastMac,bytes,sizeof(bytes));
    if (result!=ESP_OK) Serial.printf("send error: %d\n",(int)result);
  }
  delay(10);
}
