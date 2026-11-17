#pragma once
#include <stdint.h>
enum class SdServiceState : uint8_t { NotChecked, Checking, Mounted, Unavailable };
bool sdServiceRequestCheck();
SdServiceState sdServiceState();
const char *sdServiceStatusText();
uint32_t sdServiceTotalMB();
uint32_t sdServiceFreeMB();
