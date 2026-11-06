#include "contact_policy.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 // Regression cases from the user's requested interaction: walls may not jam a door,
 // and characters, physical hands, weapons and other doors must keep collision response.
 for(unsigned layer:{1u,2u,9u,13u,17u,27u,31u,35u})assert(DoorIgnoresPartnerLayer(layer));
 for(unsigned layer:{0u,4u,5u,6u,8u,15u,30u,32u,33u,34u,36u,37u,38u,39u,40u,41u,42u,43u,44u,56u,57u})assert(!DoorIgnoresPartnerLayer(layer));
 for(unsigned layer=0;layer<64;++layer)assert(PortalPlateIgnoresPartnerLayer(layer)==(layer==56 || layer==5));
 for(unsigned layer=0;layer<64;++layer)assert(PortalPlateIgnoresQueryLayer(layer)==(layer==56 || layer==5 || layer==44 || layer==40));
 for(unsigned layer:{0u,3u,4u,10u,19u,20u,26u,28u}){assert(DoorIgnoresPartner(layer,true));assert(!DoorIgnoresPartner(layer,false));}
 for(unsigned layer:{5u,6u,8u,30u,32u,33u,56u}){assert(!DoorIgnoresPartner(layer,true));assert(!DoorIgnoresPartner(layer,false));}
 puts("PASS: wall suppression; hands, weapons, NPCs, other doors and picking preserved");
}
