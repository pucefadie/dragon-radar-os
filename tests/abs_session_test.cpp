#include "abs_session.h"
#include <cassert>
#include <cstring>
int main() {
  AbsPlaybackSession session{};
  const char *good="{\"id\":\"play_1\",\"libraryItemId\":\"li_1\",\"playMethod\":0,\"duration\":100,\"currentTime\":45,\"audioTracks\":[{\"contentUrl\":\"/s/item/li_1/one.mp3\",\"mimeType\":\"audio/mpeg\",\"startOffset\":0,\"duration\":100}]}";
  assert(absParsePlaybackSession(good,strlen(good),session)); assert(session.currentTime==45 && session.count==1);
  const char *bad="{\"id\":\"play_1\",\"libraryItemId\":\"li_1\",\"playMethod\":0,\"duration\":100,\"currentTime\":45,\"audioTracks\":[{\"contentUrl\":\"https://evil.test/x\",\"mimeType\":\"audio/mpeg\",\"startOffset\":0,\"duration\":100}]}";
  assert(!absParsePlaybackSession(bad,strlen(bad),session)); assert(session.count==0);
}
