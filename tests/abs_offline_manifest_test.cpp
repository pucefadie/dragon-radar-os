#include "abs_offline_manifest.h"
#include <cassert>
#include <cstring>
int main() {
  AbsOfflineManifest m{};
  const char *good="{\"id\":\"li_1\",\"media\":{\"audioFiles\":[{\"ino\":\"123\",\"index\":1,\"mimeType\":\"audio/mpeg\",\"metadata\":{\"size\":4096}}]}}";
  assert(absParseOfflineManifest(good,strlen(good),m)); assert(m.count==1 && m.totalBytes==4096);
  assert(absOfflineFits(m,8192,4096)); assert(!absOfflineFits(m,8191,4096));
  const char *bad="{\"id\":\"../escape\",\"media\":{\"audioFiles\":[{\"ino\":\"123\",\"index\":1,\"mimeType\":\"audio/mpeg\",\"metadata\":{\"size\":4096}}]}}";
  assert(!absParseOfflineManifest(bad,strlen(bad),m)); assert(m.count==0);
  const char *big="{\"id\":\"li_1\",\"media\":{\"audioFiles\":[{\"ino\":\"123\",\"index\":1,\"mimeType\":\"audio/mp4\",\"metadata\":{\"size\":4294967296}}]}}";
  assert(!absParseOfflineManifest(big,strlen(big),m));
}
