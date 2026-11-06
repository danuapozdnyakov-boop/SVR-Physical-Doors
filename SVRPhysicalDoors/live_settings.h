#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
inline float DoorSettingValue(const char* value,float fallback,float minimum,float maximum){
 if(!value || !*value)return fallback;
 char* end=nullptr;float number=std::strtof(value,&end);
 if(end==value || !std::isfinite(number))return fallback;
 while(*end==' ' || *end=='\t' || *end=='\r' || *end=='\n')++end;
 if(*end)return fallback;
 return std::clamp(number,minimum,maximum);
}
