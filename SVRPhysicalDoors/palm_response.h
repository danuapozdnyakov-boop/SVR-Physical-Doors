#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
struct PalmResponse {float angle,speed;bool limited;};
inline bool DoorPalmStepReady(std::uint64_t prepared,std::uint64_t contact,std::uint64_t consumed){return prepared!=0 && contact==prepared && consumed!=prepared;}
inline float DoorResistance(float baseCap,float material,float heft,float inertia){
 return std::clamp(1.2f/baseCap,1.f,heft>1.f?2.5f:6.f)*material*(heft>1.f?1.f:heft)*std::clamp(inertia/3.f,1.f/6.f,4.f/3.f);
}
// Consume only impulses from a completed, real bare-hand contact step. This
// function is never applied to free flight or a grabbed leaf.
inline PalmResponse LimitPalmImpulse(float beforeAngle,float beforeSpeed,float angle,float speed,float dt,float baseCap,float material,float heft,float inertia,float strength,float grabAcceleration=8.f){
 if(!std::isfinite(beforeAngle+beforeSpeed+angle+speed+dt) || dt<=0 || dt>.05f)return {angle,speed,false};
 const float extra=std::clamp(grabAcceleration,.25f,24.f)*.85f*std::clamp(strength,.1f,2.f)*dt/DoorResistance(baseCap,material,heft,inertia);
 float result=speed;
 if(beforeSpeed*speed<0)result=std::clamp(speed,-extra,extra);
 else if(std::abs(speed)>std::abs(beforeSpeed))result=std::copysign((std::min)(std::abs(speed),std::abs(beforeSpeed)+extra),speed);
 // The solver may already have advanced the pose with an excessive contact
 // impulse. Limit that step too, without manufacturing extra displacement.
 const float maxMove=(std::max)(std::abs(beforeSpeed),std::abs(result))*dt+.0002f;
 const float movement=std::clamp(angle-beforeAngle,-maxMove,maxMove);
 const float corrected=beforeAngle+movement;
 return {corrected,result,corrected!=angle || result!=speed};
}

inline float DoorNativeInertiaScale(float inertia,float heft){return inertia*(heft>1.f?.5f:1.f);}
