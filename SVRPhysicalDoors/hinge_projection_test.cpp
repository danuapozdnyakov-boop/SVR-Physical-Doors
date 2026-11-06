#include "hinge_projection.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct V{float x,y,z;};
float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
void near(float a,float b){assert(std::abs(a-b)<.00001f);}
void audit(const HingePose<V>& pose,V origin){
 near(pose.position.x,origin.x);near(pose.position.y,origin.y);near(pose.position.z,origin.z);
 near(dot(pose.x,pose.x),1);near(dot(pose.y,pose.y),1);near(dot(pose.z,pose.z),1);
 near(dot(pose.x,pose.y),0);near(dot(pose.x,pose.z),0);near(dot(pose.y,pose.z),0);
}
int main(){
 V origin{12,-4,7};constexpr float limit=1.3962634f;
 // Ordinary local-Z hinge: pulling up/tilting must retain only the twist.
 for(float angle:{-.7f,.7f,-2.f,2.f}){
  V tilted{std::cos(angle),std::sin(angle),.8f};
  auto p=ProjectHinge(origin,V{1,0,0},V{0,1,0},V{0,0,1},V{0,0,1},V{1,0,0},tilted,0,limit,false);
  audit(p,origin);near(p.angle,std::clamp(angle,-limit,limit));near(p.z.z,1);
  auto locked=ProjectHinge(origin,V{1,0,0},V{0,1,0},V{0,0,1},V{0,0,1},V{1,0,0},tilted,0,limit,true);
  audit(locked,origin);near(locked.angle,0);
 }
 // Rotated cage models have a local-Y hinge, not a local-Z hinge.
 for(float angle:{-.7f,.7f}){
  auto p=ProjectHinge(origin,V{1,0,0},V{0,0,1},V{0,-1,0},V{0,1,0},V{1,0,0},V{std::cos(angle),std::sin(angle),.5f},0,limit,false);
  audit(p,origin);near(p.angle,angle);near(p.y.z,1);
 }
 auto degenerate=ProjectHinge(origin,V{1,0,0},V{0,1,0},V{0,0,1},V{0,0,1},V{1,0,0},V{0,0,1},.4f,limit,false);
 audit(degenerate,origin);near(degenerate.angle,.4f);
 puts("PASS: fixed hinge position; zero lift/tilt; signed opening; rotated cages; limits; locks; degenerate twist");
}
