#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#ifndef SPHERE_OTA_VERSION
#define SPHERE_OTA_VERSION 311
#endif
#define SPHERE_TEXT_(s) #s
#define SPHERE_TEXT(s) SPHERE_TEXT_(s)

void sphereOtaCancelBootDeadline();

class SphereOta {
 public:
  static constexpr uint32_t version=SPHERE_OTA_VERSION;
  static_assert(version>0 && version<=0xffffff,"DRA1 OTA version field");
  bool begin(SecureEspNow &radio) {
    uint8_t master[32]{};
    const bool ok=layout() && radio.deriveMaintenanceKey(master) &&
        mac(master,"sign",4,key_) && mac(master,"access-point",12,masterAp_);
    secureRadioClear(master,sizeof(master));
    if (!ok) return false;
    snprintf(ssid_,sizeof(ssid_),"DragonBall-%02u",DRAGON_BALL_ID);
    static const char *headers[]={"X-Manifest","X-Signature","Content-Length"};
    server_.collectHeaders(headers,3);
    server_.on("/",HTTP_GET,[this] {
      String page="<h1>DragonBall ";
      page+=DRAGON_BALL_ID;
      page+="</h1><p>Version ";
      page+=version;
      page+=R"(</p><label>Manifest <input id=m type=file></label><br>
<label>Firmware <input id=f type=file></label><button onclick=upload()>Upload</button><pre id=o></pre>
<script>async function upload(){try{const a=JSON.parse(await m.files[0].text());const x=new XMLHttpRequest();
x.open('POST','/firmware');x.setRequestHeader('X-Manifest',a.manifest);
x.setRequestHeader('X-Signature',a.signature);x.upload.onprogress=e=>o.textContent=e.loaded+' / '+e.total;
x.onload=()=>o.textContent=x.status+' '+x.responseText;x.onerror=()=>o.textContent='Connection lost';
x.send(f.files[0]);}catch(e){o.textContent=e;}}</script>)";
      server_.send(200,"text/html",page);
    });
    server_.on("/firmware",HTTP_POST,[this] {
      server_.send(reboot_ ? 200 : 400,"text/plain",
                   reboot_ ? "Verified and selected; rebooting" : "Rejected; original application remains selected");
    },[this] { receive(); });
    enabled_=true;
    return true;
  }

  static bool finishBoot(bool healthy) {
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)!=ESP_OK || state!=ESP_OTA_IMG_PENDING_VERIFY)
      return healthy;
    if (healthy && esp_ota_mark_app_valid_cancel_rollback()==ESP_OK) {
      sphereOtaCancelBootDeadline();
      return true;
    }
    esp_ota_mark_app_invalid_rollback_and_reboot();
    return false;
  }

  uint32_t command(RgbAction action,uint32_t now) {
    if (!enabled_ || active_ || requested_) return 4;
    switch (action) {
      case RgbAction::OtaInfo: return (version<<8)|2;
      case RgbAction::OtaArm: armed_=true; armedAt_=now; return 2;
      case RgbAction::OtaStart:
        if (!armed_ || now-armedAt_>=5000) return 4;
        armed_=false;
        schedule(now,1500); // Continue servicing DRA1 through the result/retry window.
        return 2;
      default: return 4;
    }
  }

  void usb(char c,uint32_t now) {
    static constexpr char command[]="ota_start";
    if (c=='\n' || c=='\r') {
      if (matched_==sizeof(command)-1 && enabled_ && !active_) schedule(now,0);
      matched_=0;
    } else if (matched_<sizeof(command)-1 && c==command[matched_]) ++matched_;
    else matched_=255;
  }

  template<typename Quiet>
  bool tick(uint32_t now,Quiet quiet) {
    if (requested_ && now-requestedAt_>=delay_) {
      if (!quiet()) return true;
      requested_=false;
      char pass[33];
      encode(masterAp_,16,pass);
      active_=WiFi.softAP(ssid_,pass,DRAGON_RADIO_CHANNEL,0,1);
      secureRadioClear(pass,sizeof(pass));
      if (active_) {
        beginAt_=now;
        server_.begin();
        Serial.printf("OTA_AP_READY %s version=%lu\n",ssid_,(unsigned long)version);
      }
    }
    if (!active_) return false;
    server_.handleClient();
    if (reboot_) { delay(100); ESP.restart(); }
    if (millis()-beginAt_>=120000 || interrupted_) {
      abort();
      server_.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      esp_wifi_set_channel(DRAGON_RADIO_CHANNEL,WIFI_SECOND_CHAN_NONE);
      active_=false;
      interrupted_=false;
      Serial.println("OTA_EXIT");
    }
    return active_;
  }

 private:
  static constexpr uint32_t slotSize=0x1c0000;
  static constexpr char imageId_[]="M3e.app:" SPHERE_TEXT(DRAGON_BALL_ID) ":" SPHERE_TEXT(SPHERE_OTA_VERSION) ";";
  static bool layout() {
    const auto *run=esp_ota_get_running_partition(), *next=esp_ota_get_next_update_partition(nullptr);
    const auto *nvs=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,nullptr);
    const auto *ota=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,nullptr);
    return run && next && run!=next && run->size==slotSize && next->size==slotSize &&
        ((run->address==0x10000 && next->address==0x1d0000)||(run->address==0x1d0000 && next->address==0x10000)) &&
        nvs && nvs->address==0x9000 && nvs->size==0x5000 && ota && ota->address==0xe000 && ota->size==0x2000;
  }
  static bool mac(const uint8_t *key,const char *data,size_t n,uint8_t *out) {
    return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),key,32,
        reinterpret_cast<const uint8_t *>(data),n,out)==0;
  }
  static void encode(const uint8_t *bytes,size_t n,char *hex) {
    static constexpr char digits[]="0123456789abcdef";
    for (size_t i=0;i<n;++i) { hex[2*i]=digits[bytes[i]>>4]; hex[2*i+1]=digits[bytes[i]&15]; }
    hex[2*n]=0;
  }
  static bool same(const String &s,const char *p,size_t n) {
    if (s.length()!=n) return false;
    unsigned d=0;
    for (size_t i=0;i<n;++i) d|=s[i]^p[i];
    return d==0;
  }
  void schedule(uint32_t now,uint32_t grace) { requested_=true; requestedAt_=now; delay_=grace; }
  void abort() {
    if (writing_) { esp_ota_abort(handle_); mbedtls_sha256_free(&hash_); writing_=false; }
  }
  void fail() {
    if (!interrupted_) {
      server_.sendHeader("Connection","close");
      server_.send(400,"text/plain","Upload rejected or interrupted; original application remains selected");
    }
    abort();
    interrupted_=true;
    server_.client().stop();
  }

  void receive() {
    auto &raw=server_.raw();
    if (raw.status==RAW_START) {
      // Referenced by the image so the uploader can verify identity before signing.
      Serial.printf("OTA_IMAGE_ID %s\n",imageId_);
      const String manifest=server_.header("X-Manifest");
      unsigned id=0, v=0, size=0;
      char digest[65]{}, expected[65], canonical[110];
      uint8_t tag[32]{};
      if (reboot_ || !layout() ||
          sscanf(manifest.c_str(),"M3E1:%u:%u:%u:%64[0123456789abcdef]",&id,&v,&size,digest)!=4 ||
          id!=DRAGON_BALL_ID || v<=version || v>0xffffff || size<256 || size>slotSize ||
          strlen(digest)!=64 || server_.header("Content-Length")!=String(size)) { fail(); return; }
      snprintf(canonical,sizeof(canonical),"M3E1:%u:%u:%u:%s",id,v,size,digest);
      if (manifest!=canonical || !mac(key_,canonical,strlen(canonical),tag)) { fail(); return; }
      encode(tag,sizeof(tag),expected);
      if (!same(server_.header("X-Signature"),expected,64)) { fail(); return; }
      expectedHash_=digest;
      snprintf(token_,sizeof(token_),"M3e.app:%u:%u;",id,v);
      tokenSize_=strlen(token_);
      match_=bytes_=0;
      found_=false;
      size_=size;
      uploadAt_=millis();
      next_=esp_ota_get_next_update_partition(nullptr);
      return;
    }
    if (raw.status==RAW_ABORTED) { fail(); return; }
    if (interrupted_ || millis()-uploadAt_>=90000) { fail(); return; }
    if (raw.status==RAW_WRITE) {
      if (raw.currentSize>size_-bytes_) { fail(); return; }
      if (!writing_) {
        esp_image_header_t header{};
        if (raw.currentSize<sizeof(header)) { fail(); return; }
        memcpy(&header,raw.buf,sizeof(header));
        if (header.magic!=ESP_IMAGE_HEADER_MAGIC || header.chip_id!=ESP_CHIP_ID_ESP32C6 ||
            esp_ota_begin(next_,size_,&handle_)!=ESP_OK) { fail(); return; }
        mbedtls_sha256_init(&hash_);
        writing_=true;
        if (mbedtls_sha256_starts(&hash_,0)!=0) { fail(); return; }
      }
      if (mbedtls_sha256_update(&hash_,raw.buf,raw.currentSize)!=0 ||
          esp_ota_write(handle_,raw.buf,raw.currentSize)!=ESP_OK) { fail(); return; }
      for (size_t i=0;i<raw.currentSize && !found_;++i) {
        const char c=char(raw.buf[i]);
        match_=c==token_[match_] ? match_+1 : size_t(c==token_[0]);
        found_=match_==tokenSize_;
      }
      bytes_+=raw.currentSize;
    } else if (raw.status==RAW_END) {
      uint8_t digest[32];
      char hex[65];
      if (!writing_ || bytes_!=size_ || !found_ || mbedtls_sha256_finish(&hash_,digest)!=0) { fail(); return; }
      encode(digest,sizeof(digest),hex);
      if (!same(expectedHash_,hex,64)) { fail(); return; }
      writing_=false;
      mbedtls_sha256_free(&hash_);
      reboot_=esp_ota_end(handle_)==ESP_OK && esp_ota_set_boot_partition(next_)==ESP_OK;
      if (!reboot_) interrupted_=true;
    }
  }

  WebServer server_{80};
  uint8_t key_[32]{},masterAp_[32]{},matched_=0;
  char ssid_[16]{},token_[32]{};
  String expectedHash_;
  const esp_partition_t *next_=nullptr;
  esp_ota_handle_t handle_{};
  mbedtls_sha256_context hash_{};
  size_t tokenSize_=0,match_=0,bytes_=0,size_=0;
  uint32_t beginAt_=0,uploadAt_=0,armedAt_=0,requestedAt_=0,delay_=0;
  bool enabled_=false,active_=false,armed_=false,requested_=false,writing_=false,interrupted_=false,reboot_=false,found_=false;
};
