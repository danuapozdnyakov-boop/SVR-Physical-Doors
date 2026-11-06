#pragma once
#include <algorithm>
#include <cmath>
// Both ordinary and teleport leaves must block the player's locomotion capsule.
// Teleport activation is independent of this scalar mass floor.
inline float DoorCapsuleMass(float current,float threshold,bool /*teleport*/,bool held){
 if(held || !std::isfinite(current) || current<=0 || !std::isfinite(threshold) || threshold<=0)return current;
 return (std::max)(current,threshold+1.f);
}
