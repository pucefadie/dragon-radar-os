#pragma once
#include <Arduino.h>
#include <nvs.h>
#include "secure_radio.h"

// Used only by the separate USB provisioning images.
inline void secureRadioProvisionPoll(Stream &console) {
  static bool used=false;
  if (used || console.available()<=0 || console.read()!='P') return;
  used=true;
  uint8_t record[SECURE_CONFIG_SIZE]{};
  SecureRadioConfig config{};
  console.setTimeout(3000);
  bool ok=console.readBytes(record,sizeof(record))==sizeof(record) &&
    secureRadioDecodeConfig(record,sizeof(record),config) &&
    config.version==SecureRecordVersion::V2;
  if (ok) {
    nvs_handle_t storage;
    ok=nvs_open(SECURE_RECORD_NAMESPACE,NVS_READWRITE,&storage)==ESP_OK;
    if (ok) {
      // Existing values, including an invalid type, are never replaced.
      ok=nvs_find_key(storage,SECURE_RECORD_V2_KEY,nullptr)==ESP_ERR_NVS_NOT_FOUND &&
        nvs_set_blob(storage,SECURE_RECORD_V2_KEY,record,sizeof(record))==ESP_OK &&
        nvs_commit(storage)==ESP_OK;
      nvs_close(storage);
    }
  }
  secureRadioClear(record,sizeof(record));
  secureRadioClear(&config,sizeof(config));
  console.println(ok?"RADIO_PROVISION_OK":"RADIO_PROVISION_REJECTED");
}
