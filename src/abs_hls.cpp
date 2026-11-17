#include "abs_hls.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <climits>
namespace {
bool parseUint(const char *p,uint32_t &out) {
  if (!p || !*p) return false;
  uint64_t n=0; for (;*p;++p) { if (*p<'0'||*p>'9') return false; n=n*10+(*p-'0'); if (n>UINT32_MAX) return false; }
  out=(uint32_t)n; return true;
}
bool safePath(const char *p) {
  if (!p || !*p || (p[0]=='/' && p[1]=='/') || strstr(p,"://") || strstr(p,"..")) return false;
  for (const unsigned char *q=(const unsigned char *)p;*q;++q)
    if (*q<=32 || *q>=127 || *q=='%' || *q=='?' || *q=='#' || *q=='\\' || *q==':') return false;
  return true;
}
}
bool absParseHls(const char *text,size_t length,uint32_t minimumSequence,AbsHlsPlaylist &out) {
  out=AbsHlsPlaylist{};
  AbsHlsPlaylist parsed{};
  if (!text || length<8 || length>65536 || memchr(text,'\0',length)) return false;
  bool first=true, haveSequence=false, haveSegment=false, pending=false; uint32_t sequence=0; float duration=0;
  size_t pos=0;
  while(pos<length) {
    size_t begin=pos; while(pos<length && text[pos]!='\n') ++pos;
    size_t n=pos-begin; if(pos<length) ++pos; if(n && text[begin+n-1]=='\r') --n;
    if(n>192) return false;
    char line[193]; memcpy(line,text+begin,n); line[n]=0;
    if(first) { if(strcmp(line,"#EXTM3U")) return false; first=false; continue; }
    if(!n) continue;
    if(!strncmp(line,"#EXT-X-MEDIA-SEQUENCE:",22)) { if(haveSequence || haveSegment || !parseUint(line+22,sequence)) return false; parsed.mediaSequence=sequence; haveSequence=true; }
    else if(!strncmp(line,"#EXTINF:",8)) {
      if(pending) return false;
      char *end=nullptr; double d=strtod(line+8,&end);
      if(end==line+8 || (*end!=',' && *end!='\0') || !std::isfinite(d) || d<=0 || d>3600) return false;
      duration=(float)d; pending=true;
    } else if(!strcmp(line,"#EXT-X-ENDLIST")) parsed.endList=true;
    else if(!strncmp(line,"#EXT-X-KEY:",11)) { if(strcmp(line+11,"METHOD=NONE")) return false; }
    else if(!strncmp(line,"#EXT-X-MAP:",11) || !strncmp(line,"#EXT-X-BYTERANGE:",17) || !strncmp(line,"#EXT-X-DISCONTINUITY",20)) return false;
    else if(line[0]=='#') continue;
    else {
      if(!pending || !safePath(line)) return false;
      haveSegment=true; pending=false;
      if(sequence>=minimumSequence) {
        if(parsed.count<ABS_HLS_SEGMENT_CAPACITY) { AbsHlsSegment &segment=parsed.segments[parsed.count++]; segment.sequence=sequence; segment.duration=duration; memcpy(segment.path,line,n+1); }
        else parsed.truncated=true;
      }
      if(sequence==UINT32_MAX) return false;
      ++sequence;
    }
  }
  if (first || !haveSegment || pending) return false;
  out=parsed; return true;
}
