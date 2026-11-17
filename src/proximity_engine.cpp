#include "proximity_engine.h"
#include <algorithm>
#include <cmath>
ProximityEngine::ProximityEngine(ProximityConfig c):cfg_(c){}
void ProximityEngine::sample(int8_t value,uint32_t now){
 raw_=value;history_[index_]=value;index_=(index_+1)%5;if(count_<5)++count_;
 int8_t sorted[5];for(uint8_t i=0;i<count_;++i)sorted[i]=history_[i];std::sort(sorted,sorted+count_);median_=sorted[count_/2];
 if(!seen_){seen_=true;ema_=median_;trendBase_=ema_;trendSince_=now;}else ema_+=0.28f*(float(median_)-ema_);
 if(uint32_t(now-trendSince_)>=2500){trendBase_=ema_;trendSince_=now;}
 lastSample_=now;
}
void ProximityEngine::update(uint32_t now){
 if(!seen_)return;
 if(uint32_t(now-lastSample_)>=cfg_.lost_ms){state_=ProximityState::Lost;pending_=state_;hasStateSince_=false;return;}
 ProximityState wanted;
 if(ema_>=cfg_.near)wanted=cfg_.found_enabled&&ema_>=cfg_.found?ProximityState::Found:ProximityState::Near;
 else if(ema_<cfg_.detected)wanted=ProximityState::Searching;
 else if(ema_-trendBase_>=cfg_.trend_db)wanted=ProximityState::Approaching;
 else if(ema_-trendBase_<=-cfg_.trend_db)wanted=ProximityState::MovingAway;
 else if(ema_>=cfg_.detected+8)wanted=ProximityState::Stable;
 else wanted=ProximityState::Detected;
 if(wanted==state_){pending_=wanted;hasStateSince_=false;return;}
 if(wanted!=pending_||!hasStateSince_){pending_=wanted;stateSince_=now;hasStateSince_=true;return;}
 uint32_t hold=wanted==ProximityState::Found?cfg_.found_hold_ms:cfg_.state_hold_ms;
 if(uint32_t(now-stateSince_)>=hold){state_=wanted;hasStateSince_=false;}
}
ProximitySnapshot ProximityEngine::snapshot(uint32_t now)const{ProximitySnapshot s{};s.seen=seen_;s.raw=raw_;s.median=median_;s.filtered=ema_;s.state=state_;s.age_ms=seen_?uint32_t(now-lastSample_):0;return s;}
void ProximityEngine::reset(){count_=index_=0;seen_=hasStateSince_=false;raw_=median_=0;ema_=trendBase_=0;lastSample_=trendSince_=stateSince_=foundSince_=0;state_=pending_=ProximityState::Searching;}
