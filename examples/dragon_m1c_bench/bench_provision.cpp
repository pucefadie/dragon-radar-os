// Linked only in the separate, explicitly selected USB provisioning image.
#if M1C_BENCH_PROVISION
#include <Arduino.h>
#include <Preferences.h>
#include "secure_radio.h"

void benchProvisionPoll() {
  static bool used = false;
  if (used || Serial.available() <= 0 || Serial.read() != 'P') return;
  used = true;
  uint8_t record[SECURE_CONFIG_SIZE]{};
  SecureRadioConfig decoded{};
  Serial.setTimeout(3000);
  bool ok = Serial.readBytes(record, sizeof(record)) == sizeof(record) &&
            secureRadioDecodeConfig(record, sizeof(record), decoded);
  if (ok) {
    Preferences prefs;
    ok = prefs.begin("dragon_radio", false);
    if (ok) {
      ok = !prefs.isKey("peer_v1") &&
           prefs.putBytes("peer_v1", record, sizeof(record)) == sizeof(record);
      prefs.end();
    }
  }
  volatile uint8_t *erase = record;
  for (size_t i=0; i<sizeof(record); ++i) erase[i] = 0;
  erase = reinterpret_cast<volatile uint8_t *>(&decoded);
  for (size_t i=0; i<sizeof(decoded); ++i) erase[i] = 0;
  Serial.println(ok ? "RADIO_PROVISION_OK" : "RADIO_PROVISION_REJECTED");
}
#endif
