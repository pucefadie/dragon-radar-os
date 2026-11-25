#pragma once
#include <stddef.h>

constexpr bool sphereNvsMutationBlocked(bool unlocked,size_t partitionAddress,bool isNvs) {
  return !unlocked && (partitionAddress==0x9000 || isNvs);
}

#ifdef ARDUINO
class Stream;
bool sphereNvsReady();
void sphereNvsUnlock();
void sphereNvsPrint(Stream &out);
#endif
