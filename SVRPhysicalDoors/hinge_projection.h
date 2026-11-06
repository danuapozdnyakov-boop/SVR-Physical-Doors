#pragma once
#include <algorithm>
#include <cmath>
template<class V> struct HingePose {V position,x,y,z,axis;float angle;};
// One degree of freedom: retain twist, discard translation and off-axis tilt.
// Works with local Y/Z hinges and arbitrary rotations of the placed reference.
template<class V> HingePose<V> ProjectHinge(V position,V x,V y,V z,V localAxis,V localPerpendicular,V movingPerpendicular,float fallbackAngle,float limit,bool locked,float minimum=NAN){
 auto dot=[](V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;};
 auto scale=[](V a,float s){return V{a.x*s,a.y*s,a.z*s};};
 auto add=[](V a,V b){return V{a.x+b.x,a.y+b.y,a.z+b.z};};
 auto cross=[](V a,V b){return V{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
 auto rotate=[&](V v){return add(add(scale(x,v.x),scale(y,v.y)),scale(z,v.z));};
 auto axis=rotate(localAxis);axis=scale(axis,1/std::sqrt((std::max)(dot(axis,axis),1e-12f)));
 auto closed=rotate(localPerpendicular);
 auto perpendicular=add(movingPerpendicular,scale(axis,-dot(movingPerpendicular,axis)));
 float angle=fallbackAngle;
 if(dot(perpendicular,perpendicular)>1e-10f){perpendicular=scale(perpendicular,1/std::sqrt(dot(perpendicular,perpendicular)));angle=std::atan2(dot(axis,cross(closed,perpendicular)),dot(closed,perpendicular));}
 angle=locked?0.f:std::clamp(angle,std::isfinite(minimum)?minimum:-limit,limit);
 auto turn=[&](V v){return add(add(scale(v,std::cos(angle)),scale(cross(axis,v),std::sin(angle))),scale(axis,dot(axis,v)*(1-std::cos(angle))));};
 return {position,turn(x),turn(y),turn(z),axis,angle};
}
