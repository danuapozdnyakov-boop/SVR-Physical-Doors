#include "release_grace.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main(){
 auto until=DoorReleaseDeadline(10);
 assert(DoorReleaseBlocked(until,10,false));
 assert(DoorReleaseBlocked(until,10.05,false)); // no early clear at the old 50ms threshold
 assert(DoorReleaseBlocked(until,10.499,false));
 assert(!DoorReleaseBlocked(until,10.5,false));
 assert(!DoorReleaseBlocked(until,10.1,true)); // explicit regrab overrides immediately
 double handDoorDeadlines[2][2]={{until,0},{0,0}};
 assert(DoorReleaseBlocked(handDoorDeadlines[0][0],10.2,false));
 assert(!DoorReleaseBlocked(handDoorDeadlines[0][1],10.2,false)); // other hand
 assert(!DoorReleaseBlocked(handDoorDeadlines[1][0],10.2,false)); // other door
 assert(!DoorReleaseBlocked(0,10.2,false)); // callback clears on regrab
 assert(!DoorReleaseBlocked(until,std::numeric_limits<double>::quiet_NaN(),false));
 puts("PASS: full 500ms, no early-clear, immediate regrab, independent hand/door, finite time");
}
