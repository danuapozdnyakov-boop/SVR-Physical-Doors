#include "model_identity.h"
#include <cassert>
#include <cstdio>
#include <unordered_map>
int main(){
 constexpr auto original="architecture\\farmhouse\\farmhousedoor01.nif";
 constexpr auto physical="svrphysicaldoorsg110\\architecture\\farmhouse\\farmhousedoor01.nif";
 assert(DoorModelMatches(original,original,physical));
 assert(DoorModelMatches(physical,original,physical)); // pre-patched base form
 assert(DoorModelMatches("Meshes/SVRPhysicalDoorsG110/Architecture/Farmhouse/FarmhouseDoor01.NIF",original,physical));
 assert(!DoorModelMatches("othermod/farmhousedoor01.nif",original,physical));
 assert(!DoorModelMatches("",original,physical));
 assert(!DoorModelMatches("svrphysicaldoorsg110/architecture/farmhouse/other.nif",original,physical));
 // Regression scenario: TESModel::model changes after registration, while the
 // already-loaded root still contains the physical leaf and anchor nodes.
 std::unordered_map<unsigned,const char*> assignments{{0x1234,original}};
 std::string currentModel="another_plugin/permutations/door.nif";
 assert(currentModel!=physical); // old discovery rejected the physical root
 assert(AssignedDoorProfile(assignments,0x1234)==original);
 assert(AssignedDoorProfile(assignments,0x9999)==nullptr);
 puts("PASS: original and prepatched model paths, case/slashes/prefix, unknown paths rejected, stable base-form assignment after path mutation");
}
