#pragma once
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace openTest {
extern std::map<std::string,std::vector<uint8_t>> storage;
extern unsigned writes;
extern bool failWrite;
}

class Preferences {
 public:
  bool begin(const char *name,bool readOnly) {
    assert(std::string(name)=="sphere_geo");
    readOnly_=readOnly;
    return true;
  }
  bool isKey(const char *key) const { return openTest::storage.count(key)!=0; }
  size_t getBytesLength(const char *key) const { return openTest::storage.at(key).size(); }
  size_t getBytes(const char *key,void *out,size_t size) const {
    const auto &bytes=openTest::storage.at(key);
    if (size<bytes.size()) return 0;
    std::memcpy(out,bytes.data(),bytes.size());
    return bytes.size();
  }
  size_t putBytes(const char *key,const void *value,size_t size) {
    assert(!readOnly_);
    ++openTest::writes;
    if (openTest::failWrite) return 0;
    const auto *bytes=static_cast<const uint8_t *>(value);
    openTest::storage[key]=std::vector<uint8_t>(bytes,bytes+size);
    return size;
  }
  void end() {}
 private:
  bool readOnly_=true;
};
