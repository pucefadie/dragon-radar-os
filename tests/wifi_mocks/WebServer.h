#pragma once
#include "Arduino.h"
#include <map>
#define HTTP_GET 0
#define HTTP_POST 1
struct WebServer {
  static std::map<std::string, void(*)()> routes;
  static std::map<std::string, String> args;
  static int lastStatus;
  explicit WebServer(int) {}
  void on(const char *path, int, void (*fn)()) { routes[path] = fn; }
  void onNotFound(void (*)()) {}
  void begin() {}
  void stop() {}
  void handleClient() {}
  void send(int code, const char *, const char *) { lastStatus = code; }
  String arg(const char *key) { return args[key]; }
};
