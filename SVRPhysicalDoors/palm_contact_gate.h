#pragma once
#include <cmath>
inline bool DoorPalmContactReady(double since,double now){return since>0 && std::isfinite(since+now) && now-since>=.5;}
