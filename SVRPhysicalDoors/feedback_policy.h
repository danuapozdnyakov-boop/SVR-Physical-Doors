#pragma once
#include <algorithm>
#include <cmath>
inline float DoorAngularSpeed(float actual,float cap,float multiplier){
    if(!std::isfinite(actual+cap+multiplier))return 0;
    return std::clamp(actual,-cap*multiplier,cap*multiplier);
}
inline bool DoorLockpickIntent(bool locked,bool freshGrab,bool allowGrab){return locked&&freshGrab&&allowGrab;}
inline bool DoorSuppressHighlight(bool disabled,bool higgsShader,bool supportedDoor){return disabled&&higgsShader&&supportedDoor;}
