#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace radarOta {
constexpr uint32_t ota0 = 0x10000, ota1 = 0x310000, slotSize = 0x300000;
constexpr uint32_t sectorSize = 0x1000, permitOffset = slotSize - sectorSize;
constexpr uint32_t imageLimit = permitOffset, descriptorOffset = 24 + 8 + 256;
constexpr uint32_t versionLimit = 999999999;
constexpr uint32_t idleMs = 120000, uploadMs = 90000;
constexpr char family[16] = "DragonRadar-M3f";
constexpr char permit[] = "DragonRadar M3f OTA0 released via ROM v1";
struct ImageIdentity {
  char family[16];
  uint32_t version;
};
static_assert(sizeof(ImageIdentity) == 20, "OTA image identity format");

constexpr uint32_t nextSlot(uint32_t running) {
  return running == ota0 ? ota1 : running == ota1 ? ota0 : 0;
}
constexpr bool mayWrite(uint32_t running, uint32_t target, bool ota0Released) {
  return target && target == nextSlot(running) && (target != ota0 || ota0Released);
}
constexpr bool elapsed(uint32_t now, uint32_t since, uint32_t timeout) {
  return uint32_t(now - since) >= timeout;
}

inline bool validIdentity(const ImageIdentity &id) {
  return memcmp(id.family, family, sizeof(family)) == 0 && id.version > 0 && id.version <= versionLimit;
}
struct Manifest {
  uint32_t version = 0, size = 0;
  char digest[65]{};
};
inline bool lowercaseHex(const char *s, size_t n) {
  if (strlen(s) != n) return false;
  for (size_t i = 0; i < n; ++i)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return true;
}
inline bool parseManifest(const char *text, size_t length, uint32_t currentVersion, Manifest &out) {
  constexpr char prefix[] = "M3F1:esp32s3:";
  if (strncmp(text, prefix, sizeof(prefix) - 1) != 0) return false;
  const char *p = text + sizeof(prefix) - 1;
  uint32_t values[2]{};
  for (auto &value : values) {
    if (*p < '1' || *p > '9') return false;
    for (unsigned digits = 0; *p >= '0' && *p <= '9'; ++digits) {
      if (digits == 9) return false;
      value = 10 * value + uint32_t(*p++ - '0');
    }
    if (*p++ != ':') return false;
  }
  if (values[0] <= currentVersion || values[1] != length ||
      length < descriptorOffset + sizeof(ImageIdentity) || length > imageLimit ||
      !lowercaseHex(p, 64)) return false;
  out.version = values[0];
  out.size = values[1];
  memcpy(out.digest, p, sizeof(out.digest));
  return true;
}

inline bool same(const char *a, const char *b, size_t len) {
  unsigned diff = 0;
  for (size_t i = 0; i < len; ++i) diff |= uint8_t(a[i]) ^ uint8_t(b[i]);
  return diff == 0;
}
inline void hex(const uint8_t *value, size_t n, char *out) {
  constexpr char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    *out++ = digits[value[i] >> 4];
    *out++ = digits[value[i] & 15];
  }
  *out = 0;
}
}
