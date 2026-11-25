#include "radar_ota_format.h"
#include <cassert>
#include <string>

int main() {
  using namespace radarOta;
  assert(!mayWrite(ota1, ota0, false));
  assert(mayWrite(ota1, ota0, true));
  assert(mayWrite(ota0, ota1, false));
  assert(!mayWrite(ota1, ota1, true));
  assert(!mayWrite(ota0, ota0, true));
  assert(!mayWrite(0x210000, ota0, true));
  assert(!mayWrite(ota0, 0, true));
  static_assert(ota0 + permitOffset == 0x30f000);
  static_assert(imageLimit % sectorSize == 0);

  const ImageIdentity identity = {"DragonRadar-M3f", 2};
  assert(validIdentity(identity));
  assert(!validIdentity(ImageIdentity{"DragonRadar-M3f", 0}));
  assert(!validIdentity(ImageIdentity{"DragonBall", 2}));

  const std::string digest(64, 'a');
  const auto check = [&](const std::string &header, size_t length, uint32_t old) {
    Manifest result{};
    const bool ok = parseManifest(header.c_str(), length, old, result);
    if (ok) {
      assert(result.size == length && result.version > old);
      assert(result.digest == digest);
    }
    return ok;
  };
  const auto manifest = "M3F1:esp32s3:2:500:" + digest;
  assert(check(manifest, 500, 1));
  assert(!check(manifest, 499, 1));
  assert(!check(manifest, 500, 2));
  assert(!check("M3F1:esp32c6:2:500:" + digest, 500, 1));
  assert(!check("M3F1:esp32s3:02:500:" + digest, 500, 1));
  assert(!check("M3F1:esp32s3:4294967298:500:" + digest, 500, 1));
  for (size_t n = 0; n < manifest.size(); ++n)
    assert(!check(manifest.substr(0, n), 500, 1));
  assert(!check(manifest + " ", 500, 1));
  assert(!check("M3F1:esp32s3:2:500:" + std::string(64, 'A'), 500, 1));
  assert(!check("M3F1:esp32s3:2:307:" + digest, 307, 1));
  assert(check("M3F1:esp32s3:2:" + std::to_string(imageLimit) + ":" + digest, imageLimit, 1));
  assert(!check("M3F1:esp32s3:2:" + std::to_string(imageLimit + 1) + ":" + digest, imageLimit + 1, 1));
  assert(same("ab", "ab", 2));
  assert(!same("ab", "ac", 2));
  const uint8_t bytes[] = {0x09, 0xaf};
  char encoded[5];
  hex(bytes, sizeof(bytes), encoded);
  assert(strcmp(encoded, "09af") == 0);

  constexpr uint32_t start = UINT32_MAX - 500;
  assert(!elapsed(start + idleMs - 1, start, idleMs));
  assert(elapsed(start + idleMs, start, idleMs));
  assert(!elapsed(start + uploadMs - 1, start, uploadMs));
  assert(elapsed(start + uploadMs, start, uploadMs));
}
