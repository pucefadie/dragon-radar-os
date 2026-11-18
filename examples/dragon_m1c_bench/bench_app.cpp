#include <Arduino.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <cstring>
#include "hw_config.h"
#include "beacon_service.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3) || M1C_BENCH_MODE != 1 || RADIO_DEV_INSECURE
#error "Build this isolated S3 sketch with M1C_BENCH_MODE=1 and DEV disabled."
#endif

#if M1C_BENCH_PROVISION
void benchProvisionPoll();
#endif

namespace {
constexpr gpio_num_t motor = static_cast<gpio_num_t>(HW_MOTOR_PIN);
static_assert(HW_MOTOR_PIN == 16 && HW_MOTOR_SAFE_OFF_LEVEL == 0,
              "Verify the stop polarity on this specific radar.");
bool motorReady = false;
bool radioReady = false;
bool fault = false;
esp_err_t nvsResult = ESP_ERR_INVALID_STATE;

#if !M1C_BENCH_PROVISION
bool setBenchChannel(uint8_t requested) {
  if (esp_wifi_set_channel(requested, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
  return beaconServiceChannel() == requested;
}

void dispatch(const char *command) {
  if (!std::strcmp(command, "beacons")) {
    beaconServicePrint(Serial);
  } else if (!std::strcmp(command, "channel1") || !std::strcmp(command, "channel6")) {
    const uint8_t channel = command[7] == '1' ? 1 : 6;
    if (!setBenchChannel(channel)) {
      Serial.println("M1C_BENCH_MODE=1 channel change failed; radio loop stopped");
      fault = true;
    } else {
      Serial.printf("M1C channel=%u\n", unsigned(channel));
    }
  } else {
    Serial.println("M1C commands: beacons, channel1, channel6");
  }
}
#endif
}

// Replaces the core call from app_main. No initVariant, automatic OTA
// confirmation, or Arduino NVS erase-and-retry on startup errors.
extern "C" void __wrap_initArduino() {
  // Set the output latch before enabling its driver, with input readback on.
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << HW_MOTOR_PIN;
  config.mode = GPIO_MODE_INPUT_OUTPUT;
  config.pull_down_en = GPIO_PULLDOWN_ENABLE;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  motorReady = gpio_set_level(motor, 0) == ESP_OK &&
               gpio_config(&config) == ESP_OK && gpio_get_level(motor) == 0;
  if (motorReady) nvsResult = nvs_flash_init();
}

void setup() {
  Serial.begin(115200);
  Serial.println("M1C_BENCH_MODE=1 ACTUATOR_CODE_EXCLUDED");
  Serial.printf("M1C GPIO16_LOW=%d NVS_INIT=%d\n", motorReady, int(nvsResult));
  if (!motorReady || nvsResult != ESP_OK) return;
#if M1C_BENCH_PROVISION
  Serial.println("M1C_BENCH_PROVISION=1 USB_ONLY");
#else
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_STA) || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      !setBenchChannel(6)) {
    Serial.println("M1C radio init failed");
    return;
  }
  beaconServiceInit();
#endif
  radioReady = true;
}

void loop() {
  if (!radioReady || fault) { delay(20); return; }
  if (gpio_get_level(motor) != 0) {
    gpio_set_level(motor, 0);
    fault = true;
    Serial.println("M1C GPIO16 readback fault; radio loop stopped");
    return;
  }
#if M1C_BENCH_PROVISION
  benchProvisionPoll();
#else
  static size_t used = 0;
  static bool overflow = false;
  static char command[20]{};
  beaconServiceUpdate();
  for (unsigned budget=0; budget<32 && Serial.available()>0; ++budget) {
    int c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (used && !overflow) { command[used] = 0; dispatch(command); }
      used = 0;
      overflow = false;
    } else if (c < 32 || c > 126 || used == sizeof(command)-1) {
      overflow = true;
    } else if (!overflow) {
      command[used++] = char(c);
    }
  }
#endif
  delay(2);
}
