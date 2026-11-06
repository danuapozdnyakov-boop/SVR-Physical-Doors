#include "held_response.h"
#include <cassert>
#include <cstdio>
int main(){
 auto wood=DoorHeldResponse(0,0,1,.01f,1.2f,1.5f);
 auto iron=DoorHeldResponse(0,0,1,.01f,1.2f,2.3f);
 auto gate=DoorHeldResponse(0,0,1,.01f,.25f,1.5f);
 assert(wood.speed>.01f && wood.speed<.03f && wood.angle<.0003f);
 assert(iron.speed<wood.speed && gate.speed<wood.speed);
 auto faster=DoorHeldResponse(0,0,1,.01f,1.2f,1.5f,8.f);
 auto slower=DoorHeldResponse(0,0,1,.01f,1.2f,1.5f,.25f);
 assert(faster.speed>wood.speed && slower.speed<wood.speed);
 auto releaseSpeed=wood.speed;assert(releaseSpeed>0);
 puts("PASS: small held acceleration, heavier material and larger gate accelerate less; no step to hand-requested angle");
}
