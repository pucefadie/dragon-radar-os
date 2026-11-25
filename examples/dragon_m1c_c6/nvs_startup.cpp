#include <Arduino.h>
#include <nvs_flash.h>
#if SPHERE_OTA
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#endif

#if !CONFIG_IDF_TARGET_ESP32C6 || !M1C_C6_NVS_GUARD || RADIO_DEV_INSECURE
#error "Build this startup only for the authenticated C6 M1c bench."
#endif

#if !M1C_C6_PROVISION
#include <WiFi.h>
#if defined(ARDUINO_XIAO_ESP32C6) && ARDUINO_USB_CDC_ON_BOOT
#include "sphere_motor_gpio.h"
#endif
#endif

#if SPHERE_OTA
static esp_timer_handle_t bootDeadline;
static void restartUnverified(void *) { esp_restart(); }
void sphereOtaCancelBootDeadline() {
  if (bootDeadline) {
    esp_timer_stop(bootDeadline);
    esp_timer_delete(bootDeadline);
    bootDeadline=nullptr;
  }
}
#endif

extern "C" void __wrap_initArduino() {
#if !M1C_C6_PROVISION && defined(ARDUINO_XIAO_ESP32C6) && ARDUINO_USB_CDC_ON_BOOT
  sphereMotorSafeLow();
#endif
#if SPHERE_OTA
  esp_ota_img_states_t appState;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(),&appState)==ESP_OK &&
      appState==ESP_OTA_IMG_PENDING_VERIFY) {
    esp_timer_create_args_t deadline{};
    deadline.callback=restartUnverified;
    deadline.name="sphere_ota_boot";
    if (esp_timer_create(&deadline,&bootDeadline)!=ESP_OK ||
        esp_timer_start_once(bootDeadline,10000000)!=ESP_OK)
      esp_ota_mark_app_invalid_rollback_and_reboot();
  }
#endif
  const esp_err_t result = nvs_flash_init();
  if (result != ESP_OK) {
#if SPHERE_OTA
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK &&
        state==ESP_OTA_IMG_PENDING_VERIFY) esp_ota_mark_app_invalid_rollback_and_reboot();
#endif
    Serial.begin(115200);
    for (;;) {
      Serial.printf("M1C_C6_NVS_GUARD=1 NVS_INIT_FAILED=%d\n", int(result));
      delay(1000);
    }
  }
  Serial.println("M1C_C6_NVS_GUARD=1");
#if !M1C_C6_PROVISION
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
#endif
}
