// Temporary, wired-only utility. Build separately from the radio firmware.
// This utility neither initializes Wi-Fi nor echoes provisioning bytes.
#include <Arduino.h>
#include <Preferences.h>
#include "secure_radio.h"

void setup() {
  Serial.begin(115200);
}
void loop() {
  static bool used=false;
  if (used || Serial.available()<=0 || Serial.read()!='P') { delay(20); return; }
  used=true;
  uint8_t bytes[SECURE_CONFIG_SIZE]{};
  SecureRadioConfig config{};
  Serial.setTimeout(3000);
  bool ok=Serial.readBytes(bytes,sizeof(bytes))==sizeof(bytes) &&
    secureRadioDecodeConfig(bytes,sizeof(bytes),config);
  if (ok) {
    Preferences prefs;
    ok=prefs.begin("dragon_radio",false);
    if (ok) {
      // Refuse replacement. A distinct maintenance procedure is needed to rotate keys.
      ok=!prefs.isKey("peer_v1") && prefs.putBytes("peer_v1",bytes,sizeof(bytes))==sizeof(bytes);
      prefs.end();
    }
  }
  volatile uint8_t *record=bytes;
  for (size_t i=0;i<sizeof(bytes);++i) record[i]=0;
  volatile uint8_t *decoded=reinterpret_cast<volatile uint8_t*>(&config);
  for (size_t i=0;i<sizeof(config);++i) decoded[i]=0;
  Serial.println(ok?"RADIO_PROVISION_OK":"RADIO_PROVISION_REJECTED");
}
