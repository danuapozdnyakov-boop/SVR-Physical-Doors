#include "door_latch.h"
#include "feedback_policy.h"
#include <cassert>
#include <cstdio>
int main(){
    for(float sign:{-1.f,1.f}){
        DoorLatch l;l.Update(0,0,true,false,false);
        l.Update(sign*.5f,sign*3.f,true,false,false);
        assert(!l.Update(sign*.2f,-sign*3.f,false,false,false));
        assert(l.Update(-sign*.2f,-sign*3.f,false,false,false)); // crossing zero in one frame still catches the latch
        assert(l.Update(sign*.2f,sign*3.f,false,false,false)); // rebound remains closed
        assert(!l.Update(0,0,true,false,false)); // next grab releases
    }
    assert(DoorAngularSpeed(3.f,1.2f,3.5f)==3.f);
    assert(DoorAngularSpeed(8.f,1.2f,3.5f)<4.21f);
    puts("PASS: forceful angular velocity retained, speed bounded and swept close latch catches slams in either direction");
}
