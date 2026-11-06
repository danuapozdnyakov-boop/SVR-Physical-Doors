#include "live_settings.h"
#include <cassert>
#include <cstdio>
int main(){
 assert(DoorSettingValue("",3,.5,4)==3);
 assert(DoorSettingValue("2.5",3,.5,4)==2.5);
 assert(DoorSettingValue("100",3,.5,4)==4);
 assert(DoorSettingValue("-100",3,.5,4)==.5);
 for(auto invalid:{"nan","inf","-inf","oops","2oops"})assert(DoorSettingValue(invalid,3,.5,4)==3);
 assert(DoorSettingValue("1.5 \r\n",3,.5,4)==1.5);
 puts("PASS: missing MCM values preserve legacy settings; malformed/nonfinite values rejected; ranges clamped");
}
