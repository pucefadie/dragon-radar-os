// Temporary, wired-only DRC2 utility. Build separately from the radio firmware.
#include "secure_radio_provision.h"

void setup() {
  Serial.begin(115200);
}

void loop() {
  secureRadioProvisionPoll(Serial);
  delay(20);
}
