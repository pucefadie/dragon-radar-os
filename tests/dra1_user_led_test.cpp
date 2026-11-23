#include <cassert>
#include "dra1_user_led.h"

int main() {
  using Status = SecureLinkStatus;
  Dra1UserLed led;
  assert(!led.update(Status::Unavailable, 0) && !led.lit());
  assert(led.update(Status::AwaitingHandshake, 10) && led.lit());
  assert(!led.update(Status::AwaitingHandshake, 509) && led.lit());
  assert(led.update(Status::AwaitingHandshake, 510) && !led.lit());
  assert(led.update(Status::ChannelMismatch, 1010) && led.lit());
  assert(led.update(Status::AwaitingHandshake, 1510) && !led.lit());

  assert(led.update(Status::Paired, 1600) && led.lit());
  assert(!led.update(Status::Paired, 2200) && led.lit());
  assert(!led.update(Status::Lost, 2300) && led.lit());
  assert(!led.update(Status::Lost, 2424) && led.lit());
  assert(led.update(Status::Lost, 2425) && !led.lit());
  assert(led.update(Status::ChannelMismatch, 2550) && led.lit());
  assert(!led.update(Status::Lost, 2800) && led.lit());
  assert(led.update(Status::Lost, 2925) && !led.lit());
  assert(led.update(Status::Paired, 2926) && led.lit());
  assert(led.update(Status::Unavailable, 2927) && !led.lit());

  Dra1UserLed wrapping;
  assert(wrapping.update(Status::AwaitingHandshake, UINT32_MAX - 249));
  assert(!wrapping.update(Status::AwaitingHandshake, 249) && wrapping.lit());
  assert(wrapping.update(Status::AwaitingHandshake, 250) && !wrapping.lit());
  assert(wrapping.update(Status::AwaitingHandshake, 750) && wrapping.lit());
}
