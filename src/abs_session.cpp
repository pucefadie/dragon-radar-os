#include "abs_session.h"
#include "abs_catalog.h"
#include <cJSON.h>
#include <cmath>
#include <cstring>
namespace {
bool identifier(const char *s) {
  if(!s || !*s || strlen(s)>64) return false;
  for(;*s;++s) if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_')) return false;
  return true;
}
bool pathOk(const char *s) {
  if(!s || s[0]!='/' || s[1]=='/' || strlen(s)>192 || strstr(s,"..")) return false;
  for(const unsigned char *p=(const unsigned char *)s;*p;++p)
    if(*p<32 || *p>=127 || *p=='\\' || *p=='?' || *p=='#' || *p=='%' || *p==':') return false;
  return true;
}
bool getNumber(const cJSON *obj,const char *key,double &v) {
  const cJSON *field=cJSON_GetObjectItemCaseSensitive(obj,key);
  if(!cJSON_IsNumber(field) || !std::isfinite(field->valuedouble) || field->valuedouble<0) return false;
  v=field->valuedouble; return true;
}
}
bool absParsePlaybackSession(const char *json,size_t length,AbsPlaybackSession &out) {
  out=AbsPlaybackSession{}; if(!absJsonDepthSafe(json,length)) return false;
  const char *end=nullptr; cJSON *root=cJSON_ParseWithLengthOpts(json,length,&end,0); if(!root) return false;
  while(end && end<json+length && (*end==' '||*end=='\r'||*end=='\n'||*end=='\t')) ++end;
  AbsPlaybackSession result{}; bool ok=cJSON_IsObject(root) && end==json+length;
  const cJSON *id=cJSON_GetObjectItemCaseSensitive(root,"id"), *item=cJSON_GetObjectItemCaseSensitive(root,"libraryItemId");
  const cJSON *method=cJSON_GetObjectItemCaseSensitive(root,"playMethod"), *tracks=cJSON_GetObjectItemCaseSensitive(root,"audioTracks");
  ok=ok && cJSON_IsString(id) && identifier(id->valuestring) && cJSON_IsString(item) && identifier(item->valuestring)
       && cJSON_IsNumber(method) && method->valuedouble>=0 && method->valuedouble<=3 && floor(method->valuedouble)==method->valuedouble
       && getNumber(root,"duration",result.duration) && result.duration>0 && getNumber(root,"currentTime",result.currentTime)
       && result.currentTime<=result.duration+1 && cJSON_IsArray(tracks) && cJSON_GetArraySize(tracks)>0;
  if(ok) {
    strcpy(result.id,id->valuestring); strcpy(result.itemId,item->valuestring); result.method=(uint8_t)method->valueint;
    const cJSON *track; cJSON_ArrayForEach(track,tracks) {
      const cJSON *path=cJSON_GetObjectItemCaseSensitive(track,"contentUrl"), *mime=cJSON_GetObjectItemCaseSensitive(track,"mimeType");
      double start=0,duration=0;
      if(!cJSON_IsObject(track)||!cJSON_IsString(path)||!pathOk(path->valuestring)||!cJSON_IsString(mime)||!mime->valuestring||!mime->valuestring[0]||strlen(mime->valuestring)>=sizeof(result.tracks[0].mime)||!getNumber(track,"startOffset",start)||!getNumber(track,"duration",duration)||duration<=0||start>result.duration+1) { ok=false; break; }
      if(result.count==ABS_SESSION_TRACKS) { result.truncated=true; continue; }
      AbsSessionTrack &entry=result.tracks[result.count++]; strcpy(entry.path,path->valuestring); strcpy(entry.mime,mime->valuestring); entry.start=start; entry.duration=duration;
    }
  }
  cJSON_Delete(root); if(!ok) return false; out=result; return true;
}
