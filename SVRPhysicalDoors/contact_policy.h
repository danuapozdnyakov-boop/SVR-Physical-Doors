#pragma once
constexpr bool PortalPlateIgnoresQueryLayer(unsigned layer){return layer==56 || layer==5 || layer==44 || layer==40;}
constexpr bool PortalPlateIgnoresPartnerLayer(unsigned layer){return layer==56 || layer==5;}
// Only used by a listener attached to this plugin's generated moving leaves.
constexpr bool DoorIgnoresPartnerLayer(unsigned layer){
 switch(layer){
  case 1:case 2:case 9:case 13:case 17:case 27:case 31:case 35: // buildings, frames, terrain
   return true;
  default:return false; // keep HIGGS hands/held items (56), weapons, actor bodies, clutter, projectiles and picking
 }
}
constexpr bool DoorIgnoresPartner(unsigned layer,bool immovable){
 if(DoorIgnoresPartnerLayer(layer))return true;
 // Keep hands, actors, weapons and moving clutter. Fixed clutter/props/windows
 // are environment even when their layer is not STATIC (e.g. furniture/frames).
 return immovable && (layer==0 || layer==3 || layer==4 || layer==10 || layer==19 || layer==20 || layer==26 || layer==28);
}
