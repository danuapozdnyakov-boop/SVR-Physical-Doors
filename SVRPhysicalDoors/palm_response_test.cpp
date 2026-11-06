#include "palm_response.h"
#include "held_response.h"
#include "door_drive.h"
#include "door_momentum.h"
#include <cassert>
#include <cstdio>
int main(){
 for(float dt:{1.f/45,1.f/72,1.f/90,1.f/120}){
  auto normal=LimitPalmImpulse(0,0,.1f,8,dt,.55f,1.5f,1,3,1);
  auto gate=LimitPalmImpulse(0,0,.1f,8,dt,.25f,1.5f,4,3,1);
  assert(normal.limited && normal.speed<1.f && gate.speed<normal.speed);
  assert(normal.angle<.025f && gate.angle<normal.angle);
  auto weak=LimitPalmImpulse(.3f,.03f,.3001f,.031f,dt,.55f,1.5f,1,3,1);
  assert(weak.speed==.031f); // no boost to a weak input
  auto brake=LimitPalmImpulse(.3f,2,.32f,.2f,dt,.55f,1.5f,1,3,1);
  assert(brake.speed==.2f); // hand may stop a moving door
  auto reverse=LimitPalmImpulse(.3f,2,.28f,-6,dt,.55f,1.5f,1,3,1);
  assert(reverse.speed>-1.f); // no instant superhuman reversal
  // A consumed contact does not remain a free-flight brake.
  float coasting=normal.speed;
  for(int i=0;i<30;++i){coasting=DoorDrivenAngularSpeed(coasting,0,0,dt,false,8);assert(coasting==normal.speed);}
 }
 auto ordinary=DoorHeldResponse(.3f,0,1,.01f,.55f,1.5f,8,6,3,1);
 auto gate=DoorHeldResponse(.3f,0,1,.01f,.25f,1.5f,8,6,3,4);
 assert(gate.speed<ordinary.speed);
 for(float heft:{1.f,1.25f}){
  auto held=DoorHeldResponse(0,0,1,.01f,.55f,1.5f,8,6,3,heft);
  auto palm=LimitPalmImpulse(0,0,.1f,9,.01f,.55f,1.5f,heft,3,1,8);
  auto weak=LimitPalmImpulse(0,0,.1f,9,.01f,.55f,1.5f,heft,3,.1f,8);
  assert(palm.speed<=held.speed && palm.speed>=held.speed*.84f);
  assert(weak.speed<palm.speed*.101f);
 }
 assert(DoorNativeInertiaScale(3,1)==3);
 assert(DoorNativeInertiaScale(3,1.25f)==1.5f);
 DoorMomentum p;p.Step(0,.3f,1,true,false,6,false);
 p.Step(.01,.31f,.2f,true,false,6,false);
 assert(p.Step(.02,.31f,0,false,false,6,false)==0); // no stale palm launch after arrest
 assert(DoorPalmStepReady(1,1,0));
 assert(!DoorPalmStepReady(1,0,0)); // nearby palm without an actual contact
 assert(!DoorPalmStepReady(2,1,0)); // the contact ended
 assert(!DoorPalmStepReady(2,2,2)); // same impulse cannot be limited twice
 puts("PASS: bounded real palm impulses and pose steps, large gates resist more, weak contact not boosted, no persistent brake or stale palm release impulse");
}
