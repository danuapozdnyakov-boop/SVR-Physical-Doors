#pragma once
#include <array>
#include <algorithm>
#include <cmath>
using PalmVec=std::array<float,3>;
struct PalmBox {PalmVec lo,hi;};
// 0: grab only, 1: existing tested bridge, 2: door-owned public-API mode.
inline int SelectDoorHandMode(bool installed,bool legacyBridge,bool api2){return !installed?0:legacyBridge?1:api2?2:0;}
inline bool PalmBoxFinite(const PalmBox& b){for(int i=0;i<3;++i)if(!std::isfinite(b.lo[i]+b.hi[i]) || b.hi[i]<=b.lo[i])return false;return true;}
inline PalmVec PalmBoxRecover(PalmVec p,const PalmBox& b){
 if(!PalmBoxFinite(b))return p;int thin=0;
 for(int i=0;i<3;++i){if(p[i]<b.lo[i]-.002f || p[i]>b.hi[i]+.002f)return p;if(b.hi[i]-b.lo[i]<b.hi[thin]-b.lo[thin])thin=i;}
 p[thin]=p[thin]<(b.lo[thin]+b.hi[thin])*.5f?b.lo[thin]-.0021f:b.hi[thin]+.0021f;return p;
}
inline PalmVec PalmBoxSlide(PalmVec a,PalmVec target,const PalmBox& b){
 if(!PalmBoxFinite(b))return target;
 a=PalmBoxRecover(a,b);float enter=0,exit=1;int axis=-1;float normal=0;
 for(int i=0;i<3;++i){float d=target[i]-a[i];if(std::abs(d)<1e-8f){if(a[i]<b.lo[i]-.002f || a[i]>b.hi[i]+.002f)return target;continue;}
  float first=(b.lo[i]-.002f-a[i])/d,last=(b.hi[i]+.002f-a[i])/d,sign=-1;
  if(first>last){std::swap(first,last);sign=1;}
  if(first>enter){enter=first;axis=i;normal=sign;}exit=(std::min)(exit,last);if(enter>exit)return target;
 }
 if(axis<0 || enter>=1 || exit<=0)return PalmBoxRecover(target,b);
 // Stop only motion into the face; preserve movement along it.
 PalmVec out=target;out[axis]=a[axis]+(target[axis]-a[axis])*enter+normal*.0001f;return out;
}
