#include "feedback_policy.h"
#include "hinge_projection.h"
#include <cassert>
#include <cstdio>
struct V{float x,y,z;};
int main(){
    assert(!DoorLockpickIntent(true,false,true)); // Proximity/contact alone never activates lockpicking.
    assert(DoorLockpickIntent(true,true,true));assert(!DoorLockpickIntent(false,true,true));
    assert(!DoorLockpickIntent(true,true,false));
    for(bool disabled:{false,true})for(bool higgs:{false,true})for(bool door:{false,true})
        assert(DoorSuppressHighlight(disabled,higgs,door)==(disabled&&higgs&&door));
    assert(DoorAngularSpeed(.4f,1,1)==.4f&&DoorAngularSpeed(-.4f,1,1)==-.4f);
    assert(DoorAngularSpeed(5,1,1)==1&&DoorAngularSpeed(-5,1,1)==-1);
    auto p=ProjectHinge(V{},V{1,0,0},V{0,1,0},V{0,0,1},V{1,0,0},V{0,1,0},V{0,.707f,-.707f},0,1.4f,false,0);
    assert(p.angle==0); // A hatch cannot be pushed down through the floor.
    auto q=ProjectHinge(V{},V{1,0,0},V{0,1,0},V{0,0,1},V{1,0,0},V{0,1,0},V{0,.707f,.707f},0,1.4f,false,0);
    assert(q.angle>.7f&&q.angle<.9f);
    puts("PASS: explicit lockpick intent, door-only HIGGS shader suppression, preserved free angular velocity, one-way horizontal hatch hinge");
}
