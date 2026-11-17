#pragma once
#include <cstdint>
#include <cstring>
#include <string>
class String : public std::string { public: using std::string::string; };
uint32_t millis();
void configTime(long, int, const char *, const char *);
