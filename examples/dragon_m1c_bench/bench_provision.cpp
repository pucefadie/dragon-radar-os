// Linked only in the separate, explicitly selected USB provisioning image.
#if M1C_BENCH_PROVISION
#include "secure_radio_provision.h"

void benchProvisionPoll() {
  secureRadioProvisionPoll(Serial);
}
#endif
