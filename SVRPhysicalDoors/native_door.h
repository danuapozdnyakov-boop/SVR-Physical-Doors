#pragma once
#include <algorithm>
#include <cmath>
inline bool NativeDoorTransition(bool teleport,int previous,int state,bool playerOpened){
 if(teleport || previous<0 || previous==state || state==0)return false;
 if(playerOpened && (state==1 || state==2))return false;
 return state>=1 && state<=4;
}
inline float NativeDoorTarget(int state,float shadow,float limit){
 if(state==3 || state==4)return 0;
 if(state==1)return std::copysign(limit,std::abs(shadow)>.01f?shadow:1.f);
 if(state==2 && std::abs(shadow)<.01f)return limit;
 return std::clamp(shadow,-limit,limit);
}

// Commit a physical close only after release, rest and latching of every leaf.
// Opening/closing animation states can be stale; snap them to the real closed pose.
inline bool DoorNeedsCloseCommit(bool teleport,bool locked,int state,bool held,bool allLatched,float resting,float nativeTime,double now,double retryUntil){
 return !teleport && !locked && !held && allLatched && resting>.25f && nativeTime<=0 && now>=retryUntil && (state==1 || state==2 || state==4);
}
