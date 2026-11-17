#include "abs_hls.h"
#include <cassert>
#include <cstring>
int main() {
  AbsHlsPlaylist p; const char *valid="#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:12\n#EXTINF:6.0,\nseg12.ts\n#EXTINF:4.5,\n/hls/seg13.ts\n#EXT-X-ENDLIST\n";
  assert(absParseHls(valid,strlen(valid),13,p)); assert(p.count==1 && p.segments[0].sequence==13 && p.segments[0].duration==4.5f && p.endList);
  const char *bad="#EXTM3U\n#EXTINF:5,\nhttps://evil.example/x.ts\n"; assert(!absParseHls(bad,strlen(bad),0,p));
  const char *encrypted="#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"x\"\n#EXTINF:5,\nx.ts\n"; assert(!absParseHls(encrypted,strlen(encrypted),0,p));
  const char *traversal="#EXTM3U\n#EXTINF:5,\n../secret\n"; assert(!absParseHls(traversal,strlen(traversal),0,p));
  const char *truncated="#EXTM3U\n#EXTINF:5,\n"; assert(!absParseHls(truncated,strlen(truncated),0,p));
  const char *discontinuity="#EXTM3U\n#EXT-X-DISCONTINUITY\n#EXTINF:5,\na.ts\n"; assert(!absParseHls(discontinuity,strlen(discontinuity),0,p));
  const char *escaped="#EXTM3U\n#EXTINF:5,\n%2e%2e/x.ts\n"; assert(!absParseHls(escaped,strlen(escaped),0,p));
}
