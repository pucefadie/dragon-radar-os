#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
class String : public std::string { public: using std::string::string; String(const std::string &s):std::string(s) {} };
class Stream { public: virtual ~Stream() = default; virtual int available() = 0; virtual int read() = 0; virtual int peek() = 0; virtual void flush() = 0; virtual size_t write(uint8_t) = 0; virtual size_t write(const uint8_t *, size_t) = 0; };
#define pdPASS 1
int xTaskCreate(void (*task)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelete(void *);
void vTaskDelay(unsigned);
struct MockSerial { template <typename... Args> void printf(const char *, Args...) {} void println(const char *) {} };
extern MockSerial Serial;
