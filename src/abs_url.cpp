#include "abs_url.h"
#include <ctype.h>
#include <string.h>
bool absNormalizeHttpsOrigin(const char *input, char *output, size_t capacity) {
  if (!input || !output || strncmp(input, "https://", 8) != 0) return false;
  const char *host = input + 8, *p = host;
  if (!*host) return false;
  while (*p && *p != ':' && *p != '/') {
    unsigned char c = (unsigned char)*p;
    if (!(isalnum(c) || c == '-' || c == '.')) return false;
    ++p;
  }
  if (p == host || p[-1] == '.' || p[-1] == '-') return false;
  if (*p == ':') {
    ++p; unsigned port = 0; const char *begin = p;
    while (*p >= '0' && *p <= '9') { port = port * 10U + (unsigned)(*p - '0'); if (port > 65535U) return false; ++p; }
    if (p == begin || port == 0) return false;
  }
  size_t length = (size_t)(p - input);
  if (*p == '/') ++p;
  if (*p || length + 1 > capacity) return false;
  memcpy(output, input, length); output[length] = 0; return true;
}
