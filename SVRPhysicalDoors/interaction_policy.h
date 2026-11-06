#pragma once
#include <cmath>
inline bool DoorOpeningMotion(float angle,float omega){
 return std::abs(angle)>.35f && angle*omega>.0035f;
}
inline bool DoorRestingClosed(float angle,float speed){
 return angle<.035f && speed<.05f;
}
