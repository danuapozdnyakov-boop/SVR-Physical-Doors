#pragma once
#include <string>
#include <string_view>
#include <cstdint>
inline std::string NormalizeDoorModel(std::string_view input){
 std::string result;result.reserve(input.size());
 for(unsigned char c:input){
  if(c=='/')c='\\';
  if(c>='A' && c<='Z')c=static_cast<unsigned char>(c+('a'-'A'));
  result.push_back(static_cast<char>(c));
 }
 // TESModel normally stores a path relative to meshes. Accept that same path
 // when another tool has supplied the conventional meshes prefix.
 if(result.starts_with("meshes\\"))result.erase(0,7);
 return result;
}
inline bool DoorModelMatches(std::string_view input,std::string_view original,std::string_view physical){
 const auto normalized=NormalizeDoorModel(input);
 return normalized==original || normalized==physical;
}
template<class Assignments> inline typename Assignments::mapped_type AssignedDoorProfile(const Assignments& assignments,std::uint32_t formId){
 auto found=assignments.find(formId);
 return found==assignments.end()?nullptr:found->second;
}
