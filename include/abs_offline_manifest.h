#pragma once
#include <stddef.h>
#include <stdint.h>
constexpr size_t ABS_OFFLINE_TRACKS = 16;
struct AbsOfflineTrack { char fileId[32]; char mime[40]; uint64_t bytes; uint16_t index; };
struct AbsOfflineManifest { char itemId[65]; uint8_t count; bool truncated; uint64_t totalBytes; AbsOfflineTrack tracks[ABS_OFFLINE_TRACKS]; };
// Read-only planning: never creates a download or writes a card.
bool absParseOfflineManifest(const char *json,size_t length,AbsOfflineManifest &out);
bool absOfflineFits(const AbsOfflineManifest &manifest,uint64_t freeBytes,uint64_t reserveBytes);
