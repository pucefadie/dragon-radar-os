#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <string>
struct Stream { std::string text; void printf(const char *fmt, ...) { char b[2048]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof(b),fmt,ap); va_end(ap); text+=b; } void println(const char *s) { text+=s; text+='\n'; } };
inline uint32_t millis() { return 100; }
