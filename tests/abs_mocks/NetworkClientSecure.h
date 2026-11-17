#pragma once
struct NetworkClientSecure {
  static bool bundleUsed;
  void useBuiltinCACertBundle() { bundleUsed = true; }
  void setHandshakeTimeout(unsigned long) {}
};
