#pragma once
#include <stddef.h>
#include <stdint.h>
constexpr size_t ABS_CATALOG_CAPACITY = 8;
struct AbsCatalogItem {
  char id[65];
  char title[129];
  char author[97];
  uint64_t progressUpdatedMs;
};
struct AbsCatalog {
  AbsCatalogItem items[ABS_CATALOG_CAPACITY];
  size_t count;
  bool truncated;
};
// Reject JSON nesting that could exhaust the embedded task stack.
bool absJsonDepthSafe(const char *json, size_t length);
// Parse a bounded items-in-progress response without accepting malformed IDs.
bool absParseCatalog(const char *json, size_t length, size_t requestedLimit, AbsCatalog *out);
