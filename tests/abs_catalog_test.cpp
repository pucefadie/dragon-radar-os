#include "abs_catalog.h"
#include <cassert>
#include <cstring>
#include <string>
static bool parse(const char *s, size_t limit, AbsCatalog &out) { return absParseCatalog(s, strlen(s), limit, &out); }
int main() {
  AbsCatalog c{};
  assert(absJsonDepthSafe("{\"s\":\"[{}]\"}", strlen("{\"s\":\"[{}]\"}")));
  std::string deep(33, '['); deep += std::string(33, ']');
  assert(!absJsonDepthSafe(deep.c_str(), deep.size()));
  assert(!absJsonDepthSafe("[{]}", 4));
  const char *good = "{\"libraryItems\":[{\"id\":\"li_1\",\"mediaType\":\"book\",\"progressLastUpdate\":100,\"media\":{\"metadata\":{\"title\":\"Book\",\"authorName\":\"Author\"}}},{\"mediaType\":\"podcast\"}]}";
  assert(parse(good, 25, c) && c.count == 1 && !c.truncated);
  assert(!strcmp(c.items[0].id,"li_1") && !strcmp(c.items[0].title,"Book") && !strcmp(c.items[0].author,"Author"));
  assert(parse(good, 2, c) && c.truncated);
  assert(parse("{\"libraryItems\":[]}",25,c) && c.count == 0);
  std::string many = "{\"libraryItems\":[";
  for (int i=0; i<9; ++i) { if (i) many += ","; many += "{\"id\":\"id_" + std::to_string(i) + "\",\"mediaType\":\"book\",\"progressLastUpdate\":2}"; }
  many += "]}";
  assert(absParseCatalog(many.c_str(), many.size(), 25, &c) && c.count == 8 && c.truncated);
  assert(!parse("{\"libraryItems\":[]}garbage",25,c));
  assert(!parse("{\"libraryItems\":[{\"id\":\"bad/id\",\"mediaType\":\"book\",\"progressLastUpdate\":1}]}",25,c));
  assert(!parse("{\"libraryItems\":[{\"id\":\"a\",\"mediaType\":\"book\",\"progressLastUpdate\":1.5}]}",25,c));
  assert(!parse("{\"libraryItems\":[{\"id\":\"a\",\"mediaType\":\"book\",\"progressLastUpdate\":1},{\"id\":\"a\",\"mediaType\":\"book\",\"progressLastUpdate\":2}]}",25,c));
}
