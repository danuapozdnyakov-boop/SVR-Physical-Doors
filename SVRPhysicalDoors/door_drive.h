#pragma once
#include <algorithm>
#include <cmath>
inline float DoorDrivenAngularSpeed(float actual,float previousCommand,float npcDesired,float dt,bool npcDriven,float cap){
 if(!std::isfinite(actual+previousCommand+npcDesired+dt+cap) || cap<=0)return 0;
 if(!npcDriven)return std::clamp(actual,-cap,cap);
 dt=std::clamp(dt,0.f,.05f);
 float desired=std::clamp(npcDesired,-cap,cap);
 return std::clamp(previousCommand+std::clamp(desired-previousCommand,-8.f*dt,8.f*dt),-cap,cap);
}
