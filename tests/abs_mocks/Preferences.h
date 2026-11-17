#pragma once
#include "Arduino.h"
#include <algorithm>
struct Preferences {
  static std::string origin, pwdOrigin, password; static int passwordWrites;
  bool begin(const char *, bool) { return true; }
  String getString(const char *key, const char *fallback) { if (!std::strcmp(key,"origin")) return String(origin); if (!std::strcmp(key,"pwd_origin")) return String(pwdOrigin); return String(fallback); }
  size_t putString(const char *key, const char *value) { if (!std::strcmp(key,"origin")) origin=value; else if (!std::strcmp(key,"pwd_origin")) pwdOrigin=value; else return 0; return std::strlen(value); }
  size_t getBytesLength(const char *key) { return !std::strcmp(key,"password") ? password.size() : 0; }
  size_t getBytes(const char *key, void *out, size_t n) { if (std::strcmp(key,"password") || n < password.size()) return 0; std::memcpy(out,password.data(),password.size()); return password.size(); }
  size_t putBytes(const char *key,const void *value,size_t n) { if (std::strcmp(key,"password")) return 0; password.assign((const char *)value,n); ++passwordWrites; return n; }
  bool remove(const char *key) { if (!std::strcmp(key,"password")) { bool exists=!password.empty(); password.clear(); return exists; } if (!std::strcmp(key,"pwd_origin")) { pwdOrigin.clear(); return true; } return false; }
  void end() {}
};
