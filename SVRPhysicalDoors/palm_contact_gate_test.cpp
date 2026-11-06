#include "palm_contact_gate.h"
#include <cassert>
#include <cstdio>
int main(){assert(!DoorPalmContactReady(10,10));assert(!DoorPalmContactReady(10,10.49));assert(DoorPalmContactReady(10,10.5));assert(DoorPalmContactReady(10,11));assert(!DoorPalmContactReady(11,11.1));assert(!DoorPalmContactReady(0,11));puts("PASS: no palm force before 500ms; continuous contact arms; reset requires another full 500ms");}
