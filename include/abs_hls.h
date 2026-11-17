#pragma once
#include <stddef.h>
#include <stdint.h>
// Parsing only. No streaming or playback is implied by a valid playlist.
constexpr size_t ABS_HLS_SEGMENT_CAPACITY = 8;
struct AbsHlsSegment { uint32_t sequence; float duration; char path[193]; };
struct AbsHlsPlaylist {
  uint32_t mediaSequence; uint16_t count; bool truncated, endList;
  AbsHlsSegment segments[ABS_HLS_SEGMENT_CAPACITY];
};
bool absParseHls(const char *text,size_t length,uint32_t minimumSequence,AbsHlsPlaylist &out);
