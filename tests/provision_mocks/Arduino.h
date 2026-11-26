#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

struct Stream {
  const uint8_t *record=nullptr;
  size_t recordSize=0;
  int command='P';
  unsigned replies=0;
  std::string reply;
  int available() const { return command>=0; }
  int read() { const int byte=command; command=-1; return byte; }
  void setTimeout(unsigned) {}
  size_t readBytes(uint8_t *buffer,size_t size) {
    if (recordSize>size) return 0;
    std::memcpy(buffer,record,recordSize);
    return recordSize;
  }
  void println(const char *value) { ++replies; reply=value; }
};
