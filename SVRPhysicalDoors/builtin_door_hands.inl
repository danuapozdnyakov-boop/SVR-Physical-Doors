// Door-owned hand interaction. No writes to PCVR memory, bodies or settings.
struct BuiltinHandHistory {bool valid=false;V safe{};RE::NiAVObject* root=nullptr;};
static BuiltinHandHistory builtinHistory[2];
struct BuiltinHandWrite {RE::NiPointer<RE::NiAVObject> node;RE::NiAVObject* root=nullptr;RE::NiPoint3 before{},after{};};
static std::array<BuiltinHandWrite,4> builtinWrites;
static bool builtinClaimed[2]{};
static void restoreBuiltinHands(){
 if(!builtinDoorHands)return;
 auto player=RE::PlayerCharacter::GetSingleton();unsigned index=0;
 for(auto& w:builtinWrites){const unsigned fp=index++%2;if(!w.node)continue;
  if(!player || player->Get3D(fp!=0)!=w.root){w.node.reset();continue;}
  auto delta=w.node->local.translate-w.after;
  if(delta.SqrLength()<.000001f){w.node->local.translate=w.before;RE::NiUpdateData u{};u.flags.set(RE::NiUpdateData::Flag::kDisableCollision);w.node->UpdateDownwardPass(u,0);}
  w.node.reset();
 }
}
static void sampleBuiltinHands(){
 if(!builtinDoorHands)return;
 auto snapshot=std::make_shared<BuiltinPalms>();auto api=HiggsAPI::g_higgs;auto player=RE::PlayerCharacter::GetSingleton();
 if(api && player && gameReady.load() && !menuSuspended.load())for(unsigned i=0;i<2;++i){
  const bool previousClaim=builtinClaimed[i];builtinClaimed[i]=false;
  if(api->IsHoldingObject(i!=0) || api->IsTwoHanding() || !api->IsHandInGrabbableState(i!=0))continue;
  auto item=player->GetEquippedObject(i!=0);
  if(item && (player->IsWeaponDrawn() || item->As<RE::TESObjectLIGH>()))continue;
  if(PcvrAPI::g_pcvr2 && PcvrAPI::g_pcvr2->IsHandClaimed(i!=0) && !previousClaim)continue;
  auto root=player->Get3D(true);auto hand=root?root->GetObjectByName(i?"NPC L Hand [LHnd]":"NPC R Hand [RHnd]"):nullptr;if(!hand)continue;
  // HIGGS PalmPosition is in Skyrim units, in the tracked hand-node frame.
  static V palmLocal=[](){char text[128]{};GetPrivateProfileStringA("Settings","PalmPosition","0,-2.4,6",text,sizeof(text),".\\Data\\SKSE\\Plugins\\higgs_vr.ini");
   float a=0,b=-2.4f,c=6;for(char* p=text;*p;++p)if(*p==',')*p=' ';if(sscanf_s(text,"%f %f %f",&a,&b,&c)!=3 || !std::isfinite(a+b+c) || std::abs(a)+std::abs(b)+std::abs(c)>50)return V{0,-2.4f,6};return V{a,b,c};}();
  auto point=hand->world*RE::NiPoint3{palmLocal.x,palmLocal.y,palmLocal.z};auto& pose=(*snapshot)[i];pose.valid=true;pose.center={point.x/69.99125f,point.y/69.99125f,point.z/69.99125f};
  const auto& r=hand->world.rotate;pose.across=unit({r.entry[0][0],r.entry[1][0],r.entry[2][0]});
  if(!std::isfinite(pose.center.x+pose.center.y+pose.center.z+pose.across.x+pose.across.y+pose.across.z) || dot(pose.across,pose.across)<.5f){pose.valid=false;continue;}
  // Borrow only an EMPTY hand near our own doors. The two-frame public API
  // lease expires automatically; never release or edit another mod's claim.
  bool nearDoor=false;
  {auto cell=player->GetParentCell();auto world=cell?cell->GetbhkWorld():nullptr;if(!world)continue;
   std::lock_guard lock(stateMutex);RE::BSReadLockGuard worldGuard(world->worldLock);for(auto& door:doors)for(auto& leaf:door.leaves){if(std::string_view(leaf.profile->leaf).starts_with("SVR_GrabOnly_"))continue;
    auto body=leaf.body?leaf.body->GetRigidBody():nullptr;if(!body || body->world!=world->GetWorld1())continue;
    RE::hkAabb box;leaf.body->GetAabbWorldspace(box);auto lo=read(box.min),hi=read(box.max);const auto p=pose.center;
    if(p.x>=lo.x-.25f && p.x<=hi.x+.25f && p.y>=lo.y-.25f && p.y<=hi.y+.25f && p.z>=lo.z-.25f && p.z<=hi.z+.25f)nearDoor=true;
  }}
  if(nearDoor && PcvrAPI::g_pcvr2){PcvrAPI::g_pcvr2->ClaimHand(i!=0,2);builtinClaimed[i]=true;}
  else builtinClaimed[i]=false;
 }
 builtinPalms.store(snapshot);
}
static void applyBuiltinHands(){
 if(!builtinDoorHands)return;
 auto player=RE::PlayerCharacter::GetSingleton();auto api=HiggsAPI::g_higgs;if(!player || !api)return;
 auto snapshot=builtinPalms.load();auto cell=player->GetParentCell();auto world=cell?cell->GetbhkWorld():nullptr;
 if(!world || !gameReady.load() || menuSuspended.load()){for(auto& h:builtinHistory)h.valid=false;return;}
 auto corrected=std::make_shared<BuiltinPalms>(*snapshot);
 std::lock_guard lock(stateMutex);RE::BSReadLockGuard worldGuard(world->worldLock);
 for(unsigned i=0;i<2;++i){auto& history=builtinHistory[i];const auto& pose=(*snapshot)[i];auto root=player->Get3D(true);
  if(!pose.valid || api->IsHoldingObject(i!=0) || api->IsTwoHanding()){history.valid=false;continue;}
  V target=pose.center,start=history.valid&&history.root==root?history.safe:target;
  auto jump=add(target,scale(start,-1));if(dot(jump,jump)>2.25f)start=target;
  V safe=target;
  for(auto& door:doors)for(auto& leaf:door.leaves){if(std::string_view(leaf.profile->leaf).starts_with("SVR_GrabOnly_"))continue;auto body=leaf.body?leaf.body->GetRigidBody():nullptr;
   if(!body || body->world!=world->GetWorld1() || !body->collidable.shape)continue;
   if(leaf.palm && (leaf.palm->ignoreHands.load()&(1u<<i)))continue;
   const auto& t=body->motion.motionState.transform;auto origin=read(t.translation),x=read(t.rotation.col0),y=read(t.rotation.col1),z=read(t.rotation.col2);
   auto local=[&](V v){auto d=add(v,scale(origin,-1));return PalmVec{dot(d,x),dot(d,y),dot(d,z)};};
   auto a=local(start),b=local(safe);if(std::abs(b[0])>15 || std::abs(b[1])>15 || std::abs(b[2])>15)continue;
   auto clamp=[&](const RE::hkpShape* shape){if(!shape || shape->type!=RE::hkpShapeType::kConvexVertices)return;
    RE::hkTransform ident{};reinterpret_cast<float*>(&ident.rotation.col0.quad)[0]=1;reinterpret_cast<float*>(&ident.rotation.col1.quad)[1]=1;reinterpret_cast<float*>(&ident.rotation.col2.quad)[2]=1;
    RE::hkAabb bounds;shape->GetAabbImpl(ident,0,bounds);auto lo=read(bounds.min),hi=read(bounds.max);b=PalmBoxSlide(a,b,{{lo.x,lo.y,lo.z},{hi.x,hi.y,hi.z}});};
   auto shape=body->collidable.shape;if(shape->type==RE::hkpShapeType::kList){auto list=static_cast<const RE::hkpListShape*>(shape);if(list->childInfo.size()<=8)for(auto& child:list->childInfo)clamp(child.shape);}else clamp(shape);
   safe=add(origin,add(add(scale(x,b[0]),scale(y,b[1])),scale(z,b[2])));
  }
  history={true,safe,root};auto delta=scale(add(safe,scale(target,-1)),69.99125f);(*corrected)[i].center=safe;
  if(dot(delta,delta)<.000001f)continue;
  for(unsigned fp=0;fp<2;++fp){auto r=player->Get3D(fp!=0);auto hand=r?r->GetObjectByName(i?"NPC L Hand [LHnd]":"NPC R Hand [RHnd]"):nullptr;if(!hand||!hand->parent)continue;
   auto& write=builtinWrites[i*2+fp];write.node=RE::NiPointer<RE::NiAVObject>(hand);write.root=r;write.before=hand->local.translate;
   auto targetPosition=hand->world.translate+RE::NiPoint3{delta.x,delta.y,delta.z};write.after=hand->parent->world.Invert()*targetPosition;hand->local.translate=write.after;
   RE::NiUpdateData u{};u.flags.set(RE::NiUpdateData::Flag::kDisableCollision);hand->UpdateDownwardPass(u,0);
  }
 }
 builtinPalms.store(corrected);
}

