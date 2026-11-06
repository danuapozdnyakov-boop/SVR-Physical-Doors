#pragma once
#include <cmath>
struct DoorLatch {
 bool latched=true,wasHeld=false,canLatch=false,haveAngle=false;float previousAngle=0;
 bool Update(float angle,float omega,bool held,bool nativeActive,bool locked){
  const float distance=std::abs(angle),zone=.05235988f;
  const float outward=angle<0?-omega:omega;
  const bool crossed=haveAngle && angle*previousAngle<0;previousAngle=angle;haveAngle=true;
  if(nativeActive){latched=false;canLatch=false;wasHeld=held;return false;}
  if(held && !wasHeld){latched=false;canLatch=false;}
  if(!held || distance>zone*1.5f)canLatch=true;
  if(!latched && canLatch && !locked && ((distance<=zone && outward<=.01f) || crossed))latched=true;
  wasHeld=held;return latched;
 }
};
constexpr bool DoorPickQuery(unsigned layer){return layer==40 || layer==44;}
constexpr bool DoorNeedsDynamicRepair(unsigned motion,bool held){return !held && (motion==4 || motion==5);}
