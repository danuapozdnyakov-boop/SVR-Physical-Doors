#include "door_momentum.h"
#include <cassert>
#include <cstdio>
int main(){
    DoorMomentum d;
    d.Step(0,.2f,0,true,false,1.2f,true);
    d.Step(.01,.21f,0,true,false,1.2f,true);
    float released=d.Step(.02,.22f,0,false,false,1.2f);assert(released>.9f && released<1.1f);
    assert(d.Step(.03,.22f,0,false,false,1.2f)==0); // No persistent motor against an obstacle.
    d.reset();d.Step(0,.2f,0,true,false,1.2f);d.Step(.01,.2f,0,true,false,1.2f);
    assert(d.Step(.02,.2f,0,false,false,1.2f)==0); // Releasing a stationary grip cannot launch a door.
    d.reset();d.Step(0,.3f,0,true,false,1.2f,true);d.Step(.01,.29f,0,true,false,1.2f,true);
    assert(d.Step(.02,.28f,0,false,false,1.2f)<-.9f);
    assert(d.Step(.03,0,1,false,true,1.2f)==0); // Locks, latch, limits and NPC servo retain authority.
    assert(d.Step(10,.3f,0,false,false,1.2f)==0); // No stale impulse after a cell/pause gap.
    d.reset();d.Step(0,.2f,0,true,false,1.2f,true);d.Step(.01,.21f,0,true,false,1.2f,true);
    assert(d.Step(.02,.22f,0,true,false,1.2f,false)>.9f); // Releasing grip works while the palm remains near the door.
    puts("PASS: measured push survives zero-velocity release; no stationary launch, continued motor, stale impulse or latch override");
}
