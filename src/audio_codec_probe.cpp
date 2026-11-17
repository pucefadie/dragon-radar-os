#include "audio_codec_probe.h"
#include <Arduino.h>
#include <esp_audio_simple_dec.h>
#include <esp_mp3_dec.h>
#include <esp_aac_dec.h>
#include <esp_ts_dec.h>
#include "codec_test_data.h"
#include <atomic>
#include <stdlib.h>
#include <string.h>
namespace {
std::atomic<bool> busy{false};
bool goodRegister(esp_audio_err_t result) { return result==ESP_AUDIO_ERR_OK || result==ESP_AUDIO_ERR_ALREADY_EXIST; }
void run(void *) {
  int mp3=esp_mp3_dec_register(), aac=esp_aac_dec_register(), ts=esp_ts_dec_register();
  bool registered=goodRegister((esp_audio_err_t)mp3) && goodRegister((esp_audio_err_t)aac) && goodRegister((esp_audio_err_t)ts);
  esp_audio_simple_dec_handle_t tsHandle=nullptr, mp3Handle=nullptr;
  esp_audio_simple_dec_cfg_t cfg={}; cfg.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_TS;
  int tsOpen=registered ? esp_audio_simple_dec_open(&cfg,&tsHandle) : ESP_AUDIO_ERR_FAIL;
  if(tsHandle) esp_audio_simple_dec_close(tsHandle);
  cfg.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
  int mp3Open=registered ? esp_audio_simple_dec_open(&cfg,&mp3Handle) : ESP_AUDIO_ERR_FAIL;
  uint8_t *input=(uint8_t *)malloc(kCodecSampleBytes), *output=(uint8_t *)malloc(8192);
  size_t offset=0,total=0; int decodeError=ESP_AUDIO_ERR_OK; esp_audio_simple_dec_info_t info={};
  if(input && output && mp3Handle) {
    memcpy(input,kCodecSampleMp3,kCodecSampleBytes);
    for(unsigned loops=0;offset<kCodecSampleBytes && loops<512;++loops) {
      esp_audio_simple_dec_raw_t raw={}; raw.buffer=input+offset; raw.len=kCodecSampleBytes-offset; raw.eos=true;
      esp_audio_simple_dec_out_t pcm={}; pcm.buffer=output; pcm.len=8192;
      int result=esp_audio_simple_dec_process(mp3Handle,&raw,&pcm);
      if(raw.consumed>raw.len || pcm.decoded_size>pcm.len) { decodeError=ESP_AUDIO_ERR_FAIL; break; }
      offset+=raw.consumed; total+=pcm.decoded_size;
      if(pcm.decoded_size) esp_audio_simple_dec_get_info(mp3Handle,&info);
      if(result!=ESP_AUDIO_ERR_OK) { if(result!=ESP_AUDIO_ERR_DATA_LACK || !total) decodeError=result; break; }
      if(!raw.consumed && !pcm.decoded_size) break;
    }
  } else decodeError=ESP_AUDIO_ERR_MEM_LACK;
  if(mp3Handle) esp_audio_simple_dec_close(mp3Handle);
  free(input); free(output);
  bool ok=registered && tsOpen==ESP_AUDIO_ERR_OK && mp3Open==ESP_AUDIO_ERR_OK && decodeError==ESP_AUDIO_ERR_OK && total>0;
  Serial.printf("[CODEC] %s MP3=%d AAC=%d TS=%d TSopen=%d MP3open=%d decode=%d PCM=%lu bytes rate=%lu channels=%u bits=%u\n",ok?"PASS":"FAIL",mp3,aac,ts,tsOpen,mp3Open,decodeError,(unsigned long)total,(unsigned long)info.sample_rate,info.channel,info.bits_per_sample);
  busy=false; vTaskDelete(nullptr);
}
}
bool audioCodecProbeStart() {
  bool expected=false; if(!busy.compare_exchange_strong(expected,true)) return false;
  if(xTaskCreate(run,"codec_probe",8192,nullptr,1,nullptr)!=pdPASS) { busy=false; return false; }
  return true;
}
