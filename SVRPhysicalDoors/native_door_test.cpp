#include "native_door.h"
#include <cassert>
#include <cstdio>
int main(){
 assert(NativeDoorTransition(false,3,2,false));
 assert(NativeDoorTransition(false,1,4,false));
 assert(!NativeDoorTransition(true,3,2,false));
 assert(!NativeDoorTransition(false,3,2,true));
 assert(!NativeDoorTransition(false,-1,1,false));
 assert(NativeDoorTarget(1,0,1.4f)==1.4f);
 assert(NativeDoorTarget(3,1,1.4f)==0);
 assert(NativeDoorTarget(4,1,1.4f)==0);
 // Full open and interrupted opening must both synchronize; closed/unknown must not.
 for(int state:{1,2,4})assert(DoorNeedsCloseCommit(false,false,state,false,true,.3f,0,10,0));
 for(int state:{0,3})assert(!DoorNeedsCloseCommit(false,false,state,false,true,.3f,0,10,0));
 assert(!DoorNeedsCloseCommit(false,false,1,true,true,.3f,0,10,0));
 assert(!DoorNeedsCloseCommit(false,false,1,false,false,.3f,0,10,0));
 assert(!DoorNeedsCloseCommit(false,false,1,false,true,.3f,1,10,0));
 assert(!DoorNeedsCloseCommit(true,false,1,false,true,.3f,0,10,0));
 assert(!DoorNeedsCloseCommit(false,true,1,false,true,.3f,0,10,0));
 assert(!DoorNeedsCloseCommit(false,false,1,false,true,.1f,0,10,0));
 assert(!DoorNeedsCloseCommit(false,false,1,false,true,.3f,0,10,10.5));
 assert(DoorNeedsCloseCommit(false,false,2,false,true,.3f,0,10.5,10.5));
 puts("PASS: ordinary NPC/script open and close, teleport exclusion, player-opening exclusion, native endpoint fallback");
}
