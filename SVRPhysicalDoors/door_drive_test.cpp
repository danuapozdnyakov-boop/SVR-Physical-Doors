#include "door_drive.h"
#include "door_momentum.h"
#include "held_response.h"
#include <cassert>
#include <cstdio>
#include <cmath>
int main(){
 // Previously: command=0.1, actual=3 -> 0.144 at 90 Hz, a 95% loss.
 for(float dt:{1.f/45,1.f/72,1.f/90,1.f/120}){
  assert(DoorDrivenAngularSpeed(3,.1f,3,dt,false,6)==3);
  assert(DoorDrivenAngularSpeed(-3,0,-3,dt,false,6)==-3);
  assert(DoorDrivenAngularSpeed(0,3,0,dt,false,6)==0); // real collision stops it
  assert(DoorDrivenAngularSpeed(.003f,3,.003f,dt,false,6)==.003f); // no minimum kick
  assert(DoorDrivenAngularSpeed(3,0,3,dt,true,6)<.18f); // NPC servo remains gradual
  DoorMomentum m;double t=0;float a=.2f,w=3;
  m.Step(t,a,0,true,false,6,true);
  t+=dt;a+=w*dt;assert(std::abs(m.Step(t,a,0,true,false,6,true)-w)<.0001f);
  t+=dt;a+=w*dt;assert(std::abs(m.Step(t,a,0,false,false,6,false)-w)<.0001f);
  // Reference: passive rotational damping, I*d(omega)/dt = -b*omega.
  for(unsigned i=0;i<90;++i){
   w*=std::exp(-.08f*dt);a+=w*dt;t+=dt;
   auto before=DoorDrivenAngularSpeed(w,.1f,w,dt,false,6);
   auto after=m.Step(t,a,before,false,false,6,false);
   assert(std::abs(after-w)<1e-5f);
  }
  assert(w>2.f);
  t+=dt;assert(m.Step(t,a,0,false,false,6,false)==0); // no motor against a stop
 }
 // A raised MCM speed limit applies equally to the grabbed response.
 auto low=DoorHeldResponse(.3f,2,1,.01f,1.2f,1.5f,24,1,3);
 auto high=DoorHeldResponse(.3f,2,1,.01f,1.2f,1.5f,24,6,3);
 assert(low.speed<2 && high.speed>2);
 auto heavy=DoorHeldResponse(.3f,0,1,.01f,1.2f,1.5f,8,6,4);
 auto light=DoorHeldResponse(.3f,0,1,.01f,1.2f,1.5f,8,6,.5f);
 assert(light.speed>heavy.speed);
 for(float direction:{-1.f,1.f}){
  auto quiet=DoorHeldResponse(.3f,0,.3f+direction*.001f,.01f,1.2f,1.5f,8,6,3);
  assert(quiet.speed==0 && quiet.angle==.3f);
  auto tap=DoorHeldResponse(.3f,0,.3f+direction*.01f,.01f,1.2f,1.5f,8,6,3);
  assert(std::abs(tap.speed)<.06f); // tiny input cannot become a fast swing
 }
 puts("PASS: no release brake at 45/72/90/120Hz, damped coasting, real stops, no minimum kick, held MCM cap/inertia, tracking-noise deadband");
}
