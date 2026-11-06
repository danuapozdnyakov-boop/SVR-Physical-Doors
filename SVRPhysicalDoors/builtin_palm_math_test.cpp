#include "builtin_palm_math.h"
#include <cassert>
#include <cstdio>
int main(){PalmBox b{{-.5f,-.03f,-1},{.5f,.03f,1}};
 assert(SelectDoorHandMode(false,false,false)==0);assert(SelectDoorHandMode(true,false,true)==2);
 assert(SelectDoorHandMode(true,true,true)==1);assert(SelectDoorHandMode(true,false,false)==0);
 auto p=PalmBoxSlide({0,-.4f,0},{0,.4f,0},b);assert(p[1]<-.03f);
 p=PalmBoxSlide({0,.4f,0},{0,-.4f,0},b);assert(p[1]>.03f);
 p=PalmBoxSlide({0,-.0321f,0},{.2f,.1f,0},b);assert(p[1]<-.03f && p[0]==.2f);
 p=PalmBoxSlide({0,-.0321f,0},{0,-.4f,0},b);assert(p[1]==-.4f);
 p=PalmBoxRecover({0,0,0},b);assert(std::abs(p[1])>.03f);
 p=PalmBoxSlide({1,-.4f,0},{1,.4f,0},b);assert(p[1]==.4f);
 puts("PASS: both faces, fast crossing, sliding, withdrawal, inside recovery, finite bounds");}
