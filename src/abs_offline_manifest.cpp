#include "abs_offline_manifest.h"
#include "abs_catalog.h"
#include <cJSON.h>
#include <cmath>
#include <cstring>
namespace {
bool safeId(const char *s,size_t max,bool numeric) {
  if (!s || !*s || strlen(s)>max) return false;
  for(;*s;++s) if(!((*s>='0'&&*s<='9')||(!numeric&&((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||*s=='-'||*s=='_')))) return false;
  return true;
}
}
bool absParseOfflineManifest(const char *json,size_t length,AbsOfflineManifest &out) {
  out=AbsOfflineManifest{}; if(!absJsonDepthSafe(json,length)) return false;
  const char *end=nullptr; cJSON *root=cJSON_ParseWithLengthOpts(json,length,&end,0); if(!root) return false;
  while(end && end<json+length && (*end==' '||*end=='\n'||*end=='\r'||*end=='\t')) ++end;
  const cJSON *id=cJSON_GetObjectItemCaseSensitive(root,"id"), *media=cJSON_GetObjectItemCaseSensitive(root,"media");
  const cJSON *files=cJSON_GetObjectItemCaseSensitive(media,"audioFiles"); AbsOfflineManifest result{};
  bool ok=cJSON_IsObject(root) && end==json+length && cJSON_IsString(id) && safeId(id->valuestring,64,false) && cJSON_IsArray(files);
  if(ok) {
    strcpy(result.itemId,id->valuestring);
    const cJSON *file; cJSON_ArrayForEach(file,files) {
      const cJSON *exclude=cJSON_GetObjectItemCaseSensitive(file,"exclude"); if(cJSON_IsTrue(exclude)) continue;
      const cJSON *ino=cJSON_GetObjectItemCaseSensitive(file,"ino"), *index=cJSON_GetObjectItemCaseSensitive(file,"index"), *mime=cJSON_GetObjectItemCaseSensitive(file,"mimeType");
      const cJSON *metadata=cJSON_GetObjectItemCaseSensitive(file,"metadata"), *size=cJSON_GetObjectItemCaseSensitive(metadata,"size");
      if(!cJSON_IsObject(file) || !cJSON_IsString(ino) || !safeId(ino->valuestring,31,true) || !cJSON_IsNumber(index) || !std::isfinite(index->valuedouble) || index->valuedouble<0 || index->valuedouble>65535 || floor(index->valuedouble)!=index->valuedouble || !cJSON_IsString(mime) || !mime->valuestring || !*mime->valuestring || strlen(mime->valuestring)>=sizeof(result.tracks[0].mime) || !cJSON_IsNumber(size) || !std::isfinite(size->valuedouble) || size->valuedouble<=0 || size->valuedouble>4294967295.0 || floor(size->valuedouble)!=size->valuedouble) { ok=false; break; }
      for(unsigned i=0;i<result.count;++i) if (!strcmp(result.tracks[i].fileId,ino->valuestring) || result.tracks[i].index==(uint16_t)index->valueint) ok=false;
      if(!ok) break;
      if(result.count==ABS_OFFLINE_TRACKS) { result.truncated=true; continue; }
      AbsOfflineTrack &track=result.tracks[result.count++]; strcpy(track.fileId,ino->valuestring); strcpy(track.mime,mime->valuestring);
      track.bytes=(uint64_t)size->valuedouble; track.index=(uint16_t)index->valueint; result.totalBytes+=track.bytes;
    }
    if(!result.count) ok=false;
  }
  cJSON_Delete(root); if(!ok) return false; out=result; return true;
}
bool absOfflineFits(const AbsOfflineManifest &m,uint64_t freeBytes,uint64_t reserveBytes) {
  return m.count>0 && !m.truncated && freeBytes>=reserveBytes && m.totalBytes<=freeBytes-reserveBytes;
}
