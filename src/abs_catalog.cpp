#include "abs_catalog.h"
#include <cJSON.h>
#include <cmath>
#include <cstring>
namespace {
bool safeId(const char *s) {
  if (!s || !*s) return false;
  size_t n = 0;
  for (; *s; ++s) { unsigned char c = (unsigned char)*s; if (++n > 64 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false; }
  return true;
}
void copyText(char *dst, size_t size, const cJSON *field) {
  if (!cJSON_IsString(field) || !field->valuestring) { dst[0] = 0; return; }
  strncpy(dst, field->valuestring, size - 1); dst[size - 1] = 0;
  // Never retain an incomplete UTF-8 continuation byte sequence at the boundary.
  size_t n = strlen(dst);
  if (n && n == size - 1) {
    size_t i = n; while (i && ((unsigned char)dst[i-1] & 0xc0) == 0x80) --i;
    if (i && ((unsigned char)dst[i-1] & 0x80)) {
      unsigned char lead = (unsigned char)dst[i-1];
      size_t expected = (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3 : (lead & 0xf8) == 0xf0 ? 4 : 1;
      if (n - (i-1) < expected) dst[i-1] = 0;
    }
  }
}
}
bool absJsonDepthSafe(const char *json, size_t length) {
  if (!json || !length || length > 65536) return false;
  char stack[32]; size_t depth = 0; bool inString = false, escaped = false;
  for (size_t i=0; i<length; ++i) {
    unsigned char c = (unsigned char)json[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') inString = false;
      else if (c < 0x20) return false;
    } else if (c == '"') inString = true;
    else if (c == '{' || c == '[') { if (depth == sizeof(stack)) return false; stack[depth++] = (char)c; }
    else if (c == '}' || c == ']') { if (!depth || (c == '}' && stack[depth-1] != '{') || (c == ']' && stack[depth-1] != '[')) return false; --depth; }
  }
  return !inString && depth == 0;
}
bool absParseCatalog(const char *json, size_t length, size_t requestedLimit, AbsCatalog *out) {
  if (!out) return false;
  memset(out, 0, sizeof(*out));
  if (!absJsonDepthSafe(json, length) || requestedLimit == 0) return false;
  const char *end = nullptr; cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, 0);
  if (!root) return false;
  while (end && end < json + length && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
  const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "libraryItems");
  bool ok = end == json + length && cJSON_IsObject(root) && cJSON_IsArray(items);
  AbsCatalog catalog{};
  if (ok) {
    size_t received = (size_t)cJSON_GetArraySize(items);
    catalog.truncated = received >= requestedLimit;
    const cJSON *item;
    cJSON_ArrayForEach(item, items) {
      const cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "mediaType");
      if (!cJSON_IsObject(item) || !cJSON_IsString(type) || !type->valuestring) { ok = false; break; }
      if (strcmp(type->valuestring, "podcast") == 0) continue;
      if (strcmp(type->valuestring, "book") != 0) { ok = false; break; }
      const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
      const cJSON *ts = cJSON_GetObjectItemCaseSensitive(item, "progressLastUpdate");
      if (!cJSON_IsString(id) || !safeId(id->valuestring) || !cJSON_IsNumber(ts) || !std::isfinite(ts->valuedouble) || ts->valuedouble < 1 || ts->valuedouble > 9007199254740991.0 || floor(ts->valuedouble) != ts->valuedouble) { ok = false; break; }
      for (size_t j=0; j<catalog.count; ++j) if (!strcmp(catalog.items[j].id, id->valuestring)) ok = false;
      if (!ok) break;
      if (catalog.count == ABS_CATALOG_CAPACITY) { catalog.truncated = true; continue; }
      AbsCatalogItem &entry = catalog.items[catalog.count++];
      strncpy(entry.id, id->valuestring, sizeof(entry.id)-1);
      entry.progressUpdatedMs = (uint64_t)ts->valuedouble;
      const cJSON *media = cJSON_GetObjectItemCaseSensitive(item, "media");
      const cJSON *meta = cJSON_GetObjectItemCaseSensitive(media, "metadata");
      copyText(entry.title, sizeof(entry.title), cJSON_GetObjectItemCaseSensitive(meta, "title"));
      const cJSON *author = cJSON_GetObjectItemCaseSensitive(meta, "authorName");
      if (!cJSON_IsString(author)) author = cJSON_GetObjectItemCaseSensitive(meta, "author");
      copyText(entry.author, sizeof(entry.author), author);
    }
  }
  if (ok) *out = catalog;
  cJSON_Delete(root); return ok;
}
