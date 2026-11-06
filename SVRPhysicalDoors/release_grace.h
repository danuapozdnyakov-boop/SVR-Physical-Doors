#pragma once
#include <cmath>
constexpr double DoorReleaseGraceSeconds=.5;
inline double DoorReleaseDeadline(double now){return std::isfinite(now)?now+DoorReleaseGraceSeconds:0;}
inline bool DoorReleaseBlocked(double until,double now,bool held){return !held && std::isfinite(until) && std::isfinite(now) && now<until;}
