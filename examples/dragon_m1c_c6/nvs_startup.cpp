#include <Arduino.h>
#include <nvs_flash.h>

#if !CONFIG_IDF_TARGET_ESP32C6 || !M1C_C6_NVS_GUARD || RADIO_DEV_INSECURE
#error "Build this startup only for the authenticated C6 M1c bench."
#endif

#if !M1C_C6_PROVISION
#include <WiFi.h>
#endif

extern "C" void __wrap_initArduino() {
  const esp_err_t result = nvs_flash_init();
  if (result != ESP_OK) {
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
