#include "abs_url.h"
#include <cassert>
#include <cstring>
int main() {
  char out[128];
  assert(absNormalizeHttpsOrigin("https://books.example.org/",out,sizeof(out)) && !strcmp(out,"https://books.example.org"));
  assert(absNormalizeHttpsOrigin("https://nas.local:8443",out,sizeof(out)));
  assert(!absNormalizeHttpsOrigin("http://books.example.org",out,sizeof(out)));
  assert(!absNormalizeHttpsOrigin("https://user:pass@host",out,sizeof(out)));
  assert(!absNormalizeHttpsOrigin("https://host/path",out,sizeof(out)));
  assert(!absNormalizeHttpsOrigin("https://host?token=secret",out,sizeof(out)));
  assert(!absNormalizeHttpsOrigin("https://host:65536",out,sizeof(out)));
  assert(!absNormalizeHttpsOrigin("https://host:0",out,sizeof(out)));
  assert(!absNormalizeHttpsOrigin("https://host",out,4));
}
