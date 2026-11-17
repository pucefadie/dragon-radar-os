#pragma once
#include <stddef.h>
#include <stdint.h>
// Parsed server metadata only; this does not open or synchronize a session.
constexpr size_t ABS_SESSION_TRACKS = 8;
struct AbsSessionTrack { char path[193]; char mime[40]; double start, duration; };
struct AbsPlaybackSession {
  char id[65], itemId[65]; double duration, currentTime; uint8_t method, count;
  bool truncated; AbsSessionTrack tracks[ABS_SESSION_TRACKS];
};
bool absParsePlaybackSession(const char *json,size_t length,AbsPlaybackSession &out);
