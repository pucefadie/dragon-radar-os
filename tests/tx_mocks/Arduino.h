#pragma once
#include <cstdint>
#include <cstring>
struct MockSerial { template<class... T> void printf(const char*,T...){} void println(const char*){} }; extern MockSerial Serial;

extern uint32_t mockMillis; inline uint32_t millis(){return mockMillis;}
