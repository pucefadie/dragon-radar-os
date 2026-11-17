#include "audio_service.h"
#include "es8311.h"
#include "settings_manager.h"
#include "i2c_bus.h"
#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>
#include <atomic>
#include "radar_beep_data.h"
namespace {
constexpr int sampleRate = 16000;
constexpr int amplifierPin = 46;
std::atomic<bool> busy{false};
std::atomic<bool> failed{false};
bool radarBeep = false;
uint8_t requestedVolume = 50;
void toneTask(void *) {
  I2SClass i2s;
  bool ok = false;
  pinMode(amplifierPin, OUTPUT); digitalWrite(amplifierPin, LOW);
  i2s.setPins(9, 45, 8, 10, 42);
  if (i2s.begin(I2S_MODE_STD, sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    es8311_handle_t codec = es8311_create(0, ES8311_ADDRESS_0);
    if (codec) {
      es8311_clock_config_t clocks = {false, false, true, sampleRate * 256, sampleRate};
      ok = es8311_init(codec, &clocks, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) == ESP_OK &&
           es8311_sample_frequency_config(codec, sampleRate * 256, sampleRate) == ESP_OK &&
           es8311_microphone_config(codec, false) == ESP_OK &&
           es8311_voice_volume_set(codec, requestedVolume, nullptr) == ESP_OK;
      if (ok) {
        digitalWrite(amplifierPin, HIGH);
        if (!radarBeep) {
          const uint8_t regs[] = {0x01,0x09,0x0d,0x12,0x13,0x14,0x31,0x32};
          Serial.printf("[AUDIO] PA GPIO46=%d | codec", digitalRead(amplifierPin));
          for (uint8_t reg : regs) { uint8_t value=0; if (i2cReadBytes(ES8311_ADDRESS_0, reg, &value, 1)) Serial.printf(" %02X=%02X", reg, value); else Serial.printf(" %02X=ERR", reg); }
          Serial.println();
        }
        int16_t samples[256 * 2];
        const int blocks = radarBeep ? (kRadarCueBytes / 2 + 255) / 256 : 75;
        for (int block = 0; block < blocks && ok; ++block) {
          bool testOn = (block < 19) || (block >= 27 && block < 46) || (block >= 54);
          for (int i = 0; i < 256; ++i) {
            const int sample = block * 256 + i;
            int16_t value = 0;
            if (radarBeep && (unsigned)(sample * 2 + 1) < kRadarCueBytes) {
              uint16_t u = kRadarCuePcm[sample * 2] | ((uint16_t)kRadarCuePcm[sample * 2 + 1] << 8);
              value = (int16_t)u;
            } else if (!radarBeep && testOn) {
              value = (int16_t)(8000.0 * sin(2.0 * PI * 880.0 * sample / sampleRate));
            }
            samples[2*i] = value; samples[2*i+1] = value;
          }
          ok = i2s.write((const uint8_t *)samples, sizeof(samples)) == sizeof(samples);
        }
        digitalWrite(amplifierPin, LOW);
      }
      es8311_delete(codec);
    }
    i2s.end();
  }
  digitalWrite(amplifierPin, LOW);
  const bool wasRadarBeep = radarBeep;
  failed = !ok; busy = false;
  if (!wasRadarBeep || !ok) Serial.println(ok ? "[AUDIO] Tono inviato: verifica acustica necessaria." : "[AUDIO] Errore I2S/codec.");
  vTaskDelete(nullptr);
}
}
namespace {
AudioResult startTone(bool radar) {
  bool expected = false;
  if (!busy.compare_exchange_strong(expected, true)) return AudioResult::Busy;
  requestedVolume = settingsGet().volume;
  radarBeep = radar; failed = false;
  if (xTaskCreate(toneTask, "audio_tone", 6144, nullptr, 1, nullptr) != pdPASS) { busy = false; failed = true; return AudioResult::Failed; }
  return AudioResult::Started;
}
}
AudioResult audioServiceTestTone() { return startTone(false); }
AudioResult audioServiceRadarBeep() { return startTone(true); }
const char *audioServiceStatus() { return busy ? "AUDIO IN CORSO" : failed ? "AUDIO FALLITO" : "TEST AUDIO DISPONIBILE"; }
