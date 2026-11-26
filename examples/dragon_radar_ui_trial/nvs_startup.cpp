#include <Arduino.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#include "ota_service.h"

extern "C" void __real_initArduino();
extern "C" esp_err_t __real_esp_partition_erase_range(const esp_partition_t *, size_t, size_t);

extern "C" esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *partition,
                                                     size_t offset, size_t size) {
  if (partition && partition->type == ESP_PARTITION_TYPE_DATA &&
      partition->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS && offset == 0 &&
      size == partition->size) {
    return ESP_ERR_INVALID_STATE;
  }
  return __real_esp_partition_erase_range(partition, offset, size);
}

extern "C" void __wrap_initArduino() {
  otaServiceBeginBoot();
  const esp_err_t result = nvs_flash_init();
  if (result == ESP_OK) {
    __real_initArduino();
    return;
  }
  Serial.begin(115200);
  for (;;) {
    Serial.printf("RADAR_UI_TRIAL NVS_INIT_FAILED=%d\n", int(result));
    delay(1000);
  }
}
