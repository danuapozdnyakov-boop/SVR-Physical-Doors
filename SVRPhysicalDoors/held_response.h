#pragma once
#include <algorithm>
#include <cmath>
#include "palm_response.h"
struct HeldResponse {float angle,speed;};
inline HeldResponse DoorHeldResponse(float angle,float speed,float requested,float dt,float baseCap,float material,float acceleration=3.f,float speedMultiplier=6.f,float inertiaMultiplier=3.f,float heft=1.f){
 float weight=DoorResistance(baseCap,material,heft,inertiaMultiplier);
 float cap=baseCap*speedMultiplier;
 float error=requested-angle;
 // Millimetre-scale tracking noise must not repeatedly kick a resting leaf.
 float desired=std::abs(error)<.0015f && std::abs(speed)<.035f?0.f:std::clamp(error/dt,-cap,cap);
 float next=speed+std::clamp(desired-speed,-acceleration/weight*dt,acceleration/weight*dt);
 return {angle+next*dt,next};
}
