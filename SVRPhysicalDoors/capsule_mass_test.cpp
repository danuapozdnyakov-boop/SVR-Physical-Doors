#include "capsule_mass.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main(){
 assert(DoorCapsuleMass(63.75f,69,false,false)==70);
 assert(DoorCapsuleMass(17,69,false,false)==70);
 assert(DoorCapsuleMass(25.5f,69,false,false)==70);
 assert(DoorCapsuleMass(150,69,false,false)==150);
 assert(DoorCapsuleMass(63.75f,69,true,false)==70);
 assert(DoorCapsuleMass(17,69,true,false)==70);
 assert(DoorCapsuleMass(150,69,true,false)==150);
 assert(DoorCapsuleMass(17,69,true,true)==17);
 assert(DoorCapsuleMass(17,69,false,true)==17);
 assert(DoorCapsuleMass(17,0,false,false)==17);
 assert(DoorCapsuleMass(80,100,false,false)==101);
 assert(DoorCapsuleMass(0,69,false,false)==0);
 assert(DoorCapsuleMass(17,std::numeric_limits<float>::quiet_NaN(),false,false)==17);
 puts("PASS: ordinary AND teleport leaves block capsule; held/heavy bodies preserved; custom threshold and invalid inputs");
}
