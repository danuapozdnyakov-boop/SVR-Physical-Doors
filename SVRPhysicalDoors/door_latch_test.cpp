#include "door_latch.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 for(float sign:{-1.f,1.f}){
  DoorLatch l;
  assert(l.Update(0,0,false,false,false));
  assert(!l.Update(0,0,true,false,false));
  assert(!l.Update(sign*.4f,sign*.2f,true,false,false));
  assert(l.Update(sign*.04f,-sign*.2f,true,false,false));
  assert(l.Update(sign*.2f,sign*2.f,true,false,false));
  assert(l.Update(0,0,false,false,false));
  assert(!l.Update(0,0,true,false,false));
  assert(!l.Update(sign*.5f,0,false,false,false));
  assert(!l.Update(0,0,false,true,false)); // NPC / native activation releases closed latch
 }
 for(unsigned layer=0;layer<64;++layer)assert(DoorPickQuery(layer)==(layer==40||layer==44));
 for(unsigned motion=0;motion<10;++motion){
  assert(!DoorNeedsDynamicRepair(motion,true));
  assert(DoorNeedsDynamicRepair(motion,false)==(motion==4||motion==5));
 }
 puts("PASS: signed door latch, next-grab release, native/NPC bypass; scoped pick queries and dynamic repair");
}
