#pragma once
#include "Arduino.h"
#include "NetworkClientSecure.h"
#define HTTPC_DISABLE_FOLLOW_REDIRECTS 0
struct HTTPClient {
  static int response, declaredSize; static bool disableRedirects; static std::string body, url;
  static bool simulateRefresh; static int refreshGets; static std::string refreshBody, catalogBody;
  void setConnectTimeout(int) {}
  void setTimeout(int) {}
  void setFollowRedirects(int mode) { disableRedirects = mode == HTTPC_DISABLE_FOLLOW_REDIRECTS; }
  bool begin(NetworkClientSecure &, const String &u) { url=u; return true; }
  void addHeader(const String &, const String &) {}
  int GET() {
    if (simulateRefresh && url.find("/api/me/items-in-progress") != std::string::npos) {
      if (refreshGets++ == 0) return 401;
      body=catalogBody; declaredSize=(int)body.size(); return 200;
    }
    return response;
  }
  int POST(uint8_t *, size_t) {
    if (simulateRefresh && url.find("/auth/refresh") != std::string::npos) {
      body=refreshBody; declaredSize=(int)body.size(); return 200;
    }
    return response;
  }
  int getSize() { return declaredSize; }
  int writeToStream(Stream *s) { size_t n=s->write((const uint8_t *)body.data(), body.size()); return n == body.size() ? (int)n : -1; }
  void end() {}
};
