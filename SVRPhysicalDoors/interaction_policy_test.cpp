#include "interaction_policy.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 for(float sign:{-1.f,1.f}){
  assert(DoorOpeningMotion(sign*.5f,sign*.2f));
  assert(!DoorOpeningMotion(sign*.5f,-sign*.2f));
  assert(!DoorOpeningMotion(sign*.5f,0));
  assert(!DoorOpeningMotion(sign*.03f,sign*.2f));
 }
 assert(DoorRestingClosed(.02f,.01f));
 assert(!DoorRestingClosed(.02f,.3f));
 assert(!DoorRestingClosed(.4f,.01f));
 puts("PASS: both opening directions; closing and stationary doors do not activate");
}
