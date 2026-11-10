#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "HiggsInterface.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <memory>
#include "models.generated.h"
#include "contact_policy.h"
#include "hinge_projection.h"
#include "interaction_policy.h"
#include "door_latch.h"
#include "hinged_lid.h"
#include "capsule_mass.h"
#include "feedback_policy.h"
#include "door_momentum.h"
#include "held_response.h"
#include "model_identity.h"
#include "live_settings.h"
#include "door_drive.h"
#include "palm_response.h"
#include "release_grace.h"
#include "native_door.h"
#include "palm_contact_gate.h"
#include "builtin_palm_math.h"
#include "PcvrInterface.h"
using namespace std::literals;
static std::ofstream logFile;

struct Settings {float angularDamping=.08f,inertia=3.f,speed=6.f,hingeFriction=.15f,grabAcceleration=8.f,palmStrength=1.f;bool lockpickOnGrab=true,showDoor=true,disableHighlight=false;};
static Settings settings;
static std::uint64_t tuningRevision=1;
struct InertiaBaseline {std::uint32_t uid;float inverse[3];};
static std::unordered_map<RE::hkpRigidBody*,InertiaBaseline> inertiaBaselines;
static RE::TESEffectShader* higgsSelection[2]{};
static void loadSettings(){
 const char* path=".\\Data\\SKSE\\Plugins\\SVRPhysicalDoors.ini";
 auto number=[&](const char* key,float fallback,float lo,float hi){char value[64]{};auto def=std::to_string(fallback);GetPrivateProfileStringA("Physics",key,def.c_str(),value,sizeof(value),path);char* end=nullptr;float result=std::strtof(value,&end);return end!=value&&std::isfinite(result)?std::clamp(result,lo,hi):fallback;};
 settings.angularDamping=number("AngularDamping",.12f,0.f,2.f);settings.inertia=number("AngularInertiaMultiplier",3.f,.5f,4.f);settings.speed=number("SpeedLimitMultiplier",6.f,.25f,16.f);settings.hingeFriction=number("HingeFrictionTorque",.15f,0.f,20.f);
 settings.lockpickOnGrab=GetPrivateProfileIntA("Interaction","LockpickOnGrab",1,path)!=0;
 settings.showDoor=GetPrivateProfileIntA("Interaction","ShowDoorDuringLockpicking",1,path)!=0;
 settings.disableHighlight=GetPrivateProfileIntA("Interaction","DisableDoorHighlight",0,path)!=0;
 // MCM Helper persists edited values here; the legacy INI remains the fallback.
 const char* user=".\\Data\\MCM\\Settings\\SVRPhysicalDoors.ini";
 auto mcm=[&](const char* section,const char* key,float fallback,float lo,float hi){
  char value[64]{};GetPrivateProfileStringA(section,key,"",value,sizeof(value),user);
  return DoorSettingValue(value,fallback,lo,hi);
 };
 settings.angularDamping=mcm("Physics","fDamping",settings.angularDamping,0.f,2.f);
 settings.inertia=mcm("Physics","fInertia",settings.inertia,.5f,4.f);
 settings.speed=mcm("Physics","fSpeedLimit",settings.speed,.25f,16.f);
 settings.hingeFriction=mcm("Physics","fFriction",settings.hingeFriction,0.f,20.f);
 settings.grabAcceleration=mcm("Physics","fGrabAcceleration",8.f,.25f,24.f);
 settings.palmStrength=mcm("Physics","fPalmStrength",1.f,.1f,2.f);
 settings.lockpickOnGrab=mcm("Interaction","bLockpickOnGrab",settings.lockpickOnGrab?1.f:0.f,0.f,1.f)!=0;
 settings.showDoor=mcm("Interaction","bShowDoorDuringLockpicking",settings.showDoor?1.f:0.f,0.f,1.f)!=0;
 settings.disableHighlight=mcm("Interaction","bDisableHighlight",settings.disableHighlight?1.f:0.f,0.f,1.f)!=0;
}

static std::atomic_bool gameReady=false;
static std::atomic<const char*> phase{"plugin loading"};
static HANDLE crashFile=INVALID_HANDLE_VALUE;
static LONG WINAPI recordFault(EXCEPTION_POINTERS* e){
 if(!e || crashFile==INVALID_HANDLE_VALUE)return EXCEPTION_CONTINUE_SEARCH;
 DWORD code=e->ExceptionRecord->ExceptionCode;
 if(code!=EXCEPTION_ACCESS_VIOLATION && code!=EXCEPTION_ILLEGAL_INSTRUCTION)return EXCEPTION_CONTINUE_SEARCH;
 static thread_local bool writing=false;if(writing)return EXCEPTION_CONTINUE_SEARCH;writing=true;
 char line[1024];DWORD written;MEMORY_BASIC_INFORMATION info{};char module[MAX_PATH]{};
 VirtualQuery(e->ExceptionRecord->ExceptionAddress,&info,sizeof(info));
 if(info.Type==MEM_IMAGE)GetModuleFileNameA(static_cast<HMODULE>(info.AllocationBase),module,MAX_PATH);
 int count=snprintf(line,sizeof(line),"exception=%08lX phase=%s module=%s offset=%llX rip=%llX rsp=%llX\r\n",code,phase.load(),module,
  (unsigned long long)((uintptr_t)e->ExceptionRecord->ExceptionAddress-(uintptr_t)info.AllocationBase),
  (unsigned long long)e->ContextRecord->Rip,(unsigned long long)e->ContextRecord->Rsp);
 if(count>0)WriteFile(crashFile,line,(DWORD)(std::min)(count,(int)sizeof(line)-1),&written,nullptr);
 uintptr_t stack[64]{};SIZE_T size=0;
 if(ReadProcessMemory(GetCurrentProcess(),(void*)e->ContextRecord->Rsp,stack,sizeof(stack),&size)){
  for(unsigned i=0;i<size/sizeof(uintptr_t);++i){
   MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery((void*)stack[i],&m,sizeof(m)) || m.Type!=MEM_IMAGE)continue;
   if(!(m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))continue;
   module[0]=0;GetModuleFileNameA(static_cast<HMODULE>(m.AllocationBase),module,MAX_PATH);
   count=snprintf(line,sizeof(line),"stack_candidate[%u]=%s+%llX\r\n",i,module,(unsigned long long)(stack[i]-(uintptr_t)m.AllocationBase));
   if(count>0)WriteFile(crashFile,line,(DWORD)(std::min)(count,(int)sizeof(line)-1),&written,nullptr);
  }
 }
 FlushFileBuffers(crashFile);writing=false;
 return EXCEPTION_CONTINUE_SEARCH;  // Diagnostic only: never suppress or resume a game fault.
}
struct V {float x,y,z;};
static V read(const RE::hkVector4& v){return {v.quad.m128_f32[0],v.quad.m128_f32[1],v.quad.m128_f32[2]};}
static float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static V scale(V a,float s){return {a.x*s,a.y*s,a.z*s};}
static V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static V unit(V a){return scale(a,1/std::sqrt((std::max)(dot(a,a),1e-12f)));}
static RE::hkVector4 packed(V a){RE::hkVector4 v;v.quad=_mm_set_ps(0,a.z,a.y,a.x);return v;}
static RE::hkQuaternion quaternion(V x,V y,V z){
 float qx,qy,qz,qw;float trace=x.x+y.y+z.z;
 if(trace>0){float s=std::sqrt(trace+1)*2;qw=s/4;qx=(y.z-z.y)/s;qy=(z.x-x.z)/s;qz=(x.y-y.x)/s;}
 else if(x.x>y.y && x.x>z.z){float s=std::sqrt(1+x.x-y.y-z.z)*2;qw=(y.z-z.y)/s;qx=s/4;qy=(y.x+x.y)/s;qz=(z.x+x.z)/s;}
 else if(y.y>z.z){float s=std::sqrt(1+y.y-x.x-z.z)*2;qw=(z.x-x.z)/s;qx=(y.x+x.y)/s;qy=s/4;qz=(z.y+y.z)/s;}
 else{float s=std::sqrt(1+z.z-x.x-y.y)*2;qw=(x.y-y.x)/s;qx=(z.x+x.z)/s;qy=(z.y+y.z)/s;qz=s/4;}
 RE::hkQuaternion q;q.vec.quad=_mm_set_ps(qw,qz,qy,qx);return q;
}
static V rotate(RE::hkpRigidBody* b,V v){
 auto& m=b->motion.motionState.transform.rotation;
 auto x=read(m.col0),y=read(m.col1),z=read(m.col2);
 return add(add(scale(x,v.x),scale(y,v.y)),scale(z,v.z));
}
struct PalmContactState {
 explicit PalmContactState(std::uint32_t id,std::uint32_t ref):uid(id),refID(ref){}
 const std::uint32_t uid,refID;
 bool permeable=false;std::atomic_uint actorContact=0;
 std::atomic_uint64_t prepared=0,contact=0;std::atomic_uint64_t palmSeen[2]{};std::atomic<double> palmSince[2]{};
 std::atomic_uint ignoreHands=0;unsigned heldHands=0;std::atomic<double> releaseUntil[2]{};
 std::uint64_t consumed=0;
 float angle=0,speed=0,dt=.011f;
};
using PalmContacts=std::unordered_map<RE::hkpRigidBody*,std::shared_ptr<PalmContactState>>;
static std::atomic<std::shared_ptr<const PalmContacts>> palmContacts{std::make_shared<const PalmContacts>()};
static std::atomic<RE::hkpRigidBody*> palmBodies[4];
static std::uint64_t palmStep=0;
static bool pcvrPresent=false,builtinDoorHands=false;
struct BuiltinPalm {bool valid=false;V center{},across{};};
using BuiltinPalms=std::array<BuiltinPalm,2>;
static std::atomic<std::shared_ptr<const BuiltinPalms>> builtinPalms{std::make_shared<const BuiltinPalms>()};
static std::atomic_uint64_t disabledDoorContacts=0;
static std::unordered_map<RE::hkpRigidBody*,std::uint32_t> registeredContacts;
struct DoorContactListener final:RE::hkpContactListener{
 void ContactPointCallback(const RE::hkpContactPointEvent& e) override{
  const unsigned source=unsigned(e.source);if(source>1 || !e.contactPointProperties)return;
  auto partner=e.bodies[1-source];if(!partner)return;
  const unsigned layer=partner->collidable.broadPhaseHandle.collisionFilterInfo.filter&0x7f;
  auto owner=e.bodies[source];auto snapshot=palmContacts.load();auto stateIt=snapshot->find(owner);
  if(stateIt!=snapshot->end() && owner && stateIt->second->uid==owner->uid){
   if(layer==8 || layer==30 || layer==33){
    auto ref=partner->GetUserData();auto actor=ref?ref->As<RE::Actor>():nullptr;
    if(actor && actor!=RE::PlayerCharacter::GetSingleton())stateIt->second->actorContact.store(actor->GetFormID());
   }
   // Door-vs-active-ragdoll impacts can cause PLANCK clutter damage/crushing.
   // Retain the character-controller capsule (30) to block walking.
   if(layer==8 || layer==33){e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);return;}
  }
  unsigned handIndex=4;
  for(unsigned i=0;i<4;++i)if(partner==palmBodies[i].load()){handIndex=i%2;break;}
  if(handIndex<2){
   if(stateIt!=snapshot->end() && stateIt->second->permeable){e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);return;}
   if(!pcvrPresent){e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);return;}
   if(builtinDoorHands){
    auto poses=builtinPalms.load();const auto& palm=(*poses)[handIndex];
    if(!palm.valid || !e.contactPoint){e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);return;}
    auto delta=add(read(e.contactPoint->position),scale(palm.center,-1));
    float along=std::clamp(dot(delta,palm.across),-.035f,.035f);
    delta=add(delta,scale(palm.across,-along));
    if(dot(delta,delta)>.032f*.032f){e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);return;}
   }
   // HIGGS's interaction slab can extend well beyond the actual palm. Use
   // PCVR's hand-sized capsule as a narrow contact gate, without resizing any
   // shared HIGGS shape. This does not create an impulse at a distance.
   auto narrow=palmBodies[handIndex+2].load();
   if(!e.contactPoint || (partner!=narrow && !narrow && !builtinDoorHands) || reinterpret_cast<const float*>(&e.contactPoint->separatingNormal.quad)[3]>.002f){
    e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);return;
   }
   if(narrow && narrow!=partner && narrow->world==partner->world && narrow->collidable.shape && e.contactPoint){
    auto shape=narrow->collidable.shape;
    if(shape->type==RE::hkpShapeType::kCapsule){
     auto capsule=static_cast<const RE::hkpCapsuleShape*>(shape);
     auto p=add(read(e.contactPoint->position),scale(read(narrow->motion.motionState.transform.translation),-1));
     const auto& r=narrow->motion.motionState.transform.rotation;
     V local{dot(p,read(r.col0)),dot(p,read(r.col1)),dot(p,read(r.col2))};
     auto a=read(capsule->vertexA),b=read(capsule->vertexB),ab=add(b,scale(a,-1)),ap=add(local,scale(a,-1));
     float length=dot(ab,ab);float t=length>1e-10f?std::clamp(dot(ap,ab)/length,0.f,1.f):0;
     auto delta=add(ap,scale(ab,-t));float radius=capsule->radius+.002f;
     if(dot(delta,delta)>radius*radius){e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);return;}
    }
   }
   auto owner=e.bodies[source];auto contacts=palmContacts.load();auto found=contacts->find(owner);
   if(found!=contacts->end() && owner && found->second->uid==owner->uid)
    {
     if(found->second->ignoreHands.load()&(1u<<handIndex)){
      e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);return;
     }
     auto& state=*found->second;auto serial=state.prepared.load();
     auto previous=state.palmSeen[handIndex].exchange(serial);
     const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
     if(!previous || (previous!=serial && serial!=previous+1))state.palmSince[handIndex].store(now);
     if(!DoorPalmContactReady(state.palmSince[handIndex].load(),now)){
      e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);
      if(e.contactMgr)e.contactMgr->contactPointCallbackDelay=0;
      return;
     }
     state.contact.store(serial);
    }
  }
  auto motion=*partner->motion.type;
  bool immovable=motion==RE::hkpMotion::MotionType::kFixed || motion==RE::hkpMotion::MotionType::kKeyframed;
  auto own=registeredContacts.find(partner);
  const bool anotherLeaf=own!=registeredContacts.end() && own->second==partner->uid;
  if(!anotherLeaf && !DoorIgnoresPartner(layer,immovable))return;
  // HIGGS uses this same solver flag. No global collision table or other objects are modified.
  e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);
  if(e.contactMgr)e.contactMgr->contactPointCallbackDelay=0;
  disabledDoorContacts.fetch_add(1,std::memory_order_relaxed);
 }
};
static DoorContactListener doorContacts;
struct PortalBackingListener final:RE::hkpContactListener{
 void ContactPointCallback(const RE::hkpContactPointEvent& e) override{
  unsigned source=unsigned(e.source);if(source>1 || !e.contactPointProperties)return;
  auto partner=e.bodies[1-source];if(!partner)return;
  unsigned layer=partner->collidable.broadPhaseHandle.collisionFilterInfo.filter&0x7f;
  // Only the hidden solid portal plate: keep its actor/world contacts and the real frame.
  if(!PortalPlateIgnoresPartnerLayer(layer))return;
  e.contactPointProperties->flags.set(RE::hkContactPointMaterial::Flag::kIsDisabled);
  if(e.contactMgr)e.contactMgr->contactPointCallbackDelay=0;
 }
};
static PortalBackingListener portalBackingContacts;
static std::unordered_map<RE::hkpRigidBody*,std::uint32_t> registeredBackings;
static std::atomic<std::shared_ptr<const std::unordered_set<std::uint32_t>>> portalFilters{std::make_shared<const std::unordered_set<std::uint32_t>>()};
// Read-only bridge for the user's existing PCVR binary. The code signature and
// timestamp are checked before the group address is used. No stack walking in a
// collision callback, and no character-controller layer-wide exception.
static volatile const std::uint32_t* pcvrHandRayGroup=nullptr;
static std::uint32_t(*pcvrHandRayGetter)()=nullptr;
static RE::hkpRigidBody*(*pcvrBareHandGetter)(bool)=nullptr;
static std::atomic<std::shared_ptr<const std::unordered_set<std::uint32_t>>> leafPickFilters{std::make_shared<const std::unordered_set<std::uint32_t>>()};
static std::atomic<std::shared_ptr<const std::unordered_set<std::uint32_t>>> grilleFilters{std::make_shared<const std::unordered_set<std::uint32_t>>()};
static bool isPcvrHandRay(std::uint32_t info){
 if((info&0x7f)!=30 || (!pcvrHandRayGroup && !pcvrHandRayGetter))return false;
 auto group=pcvrHandRayGetter?pcvrHandRayGetter():*pcvrHandRayGroup;return group!=0 && group==(info>>16);
}
static HiggsAPI::IHiggsInterface001::CollisionFilterComparisonResult comparePortal(void*,std::uint32_t a,std::uint32_t b){
 using R=HiggsAPI::IHiggsInterface001::CollisionFilterComparisonResult;
 auto leaves=leafPickFilters.load(std::memory_order_acquire);
 auto actorBody=[](unsigned layer){return layer==8 || layer==33;};
 // Reject active-ragdoll body pairs before contacts exist, preventing clutter-hit damage callbacks.
 if((leaves->contains(a) && actorBody(b&0x7f)) || (leaves->contains(b) && actorBody(a&0x7f)))return R::Ignore;
 auto grilles=grilleFilters.load();
 if((grilles->contains(a) && ((b&0x7f)==56 || isPcvrHandRay(b))) || (grilles->contains(b) && ((a&0x7f)==56 || isPcvrHandRay(a))))return R::Ignore;
 if((leaves->contains(a) && (DoorPickQuery(b&0x7f)||isPcvrHandRay(b))) || (leaves->contains(b) && (DoorPickQuery(a&0x7f)||isPcvrHandRay(a))))return R::Collide;
 auto filters=portalFilters.load(std::memory_order_acquire);
 // Selection/linear casts must agree with solver contacts; actual frames are never tagged.
 if((filters->contains(a) && (PortalPlateIgnoresQueryLayer(b&0x7f)||isPcvrHandRay(b))) || (filters->contains(b) && (PortalPlateIgnoresQueryLayer(a&0x7f)||isPcvrHandRay(a))))return R::Ignore;
 return R::Continue;
}
struct Leaf {HingedLid lid;DoorLatch latch;std::uint32_t pickGroup=0;RE::bhkRigidBody* body=nullptr; RE::bhkRigidBody* anchor=nullptr; RE::NiAVObject* node=nullptr;RE::NiAVObject* anchorNode=nullptr;RE::NiAVObject* shadow=nullptr;RE::hkpRigidBody* contactBody=nullptr;std::uint32_t contactUid=0;const LeafProfile* profile=nullptr;std::uint32_t poseUid=0;bool poseInitialized=false;float commandedSpeed=0;float angle=0;std::uint32_t tunedUid=0;std::uint64_t tunedRevision=0;DoorMomentum momentum;std::shared_ptr<PalmContactState> palm;};
static PalmResponse TakePalmResponse(Leaf& l,float angle,float speed,bool held){
 auto state=l.palm;
 if(!state)return {angle,speed,false};
 auto prepared=state->prepared.load();
 if(!DoorPalmStepReady(prepared,state->contact.load(),state->consumed))return {angle,speed,false};
 state->consumed=prepared;
 if(held || !l.profile)return {angle,speed,false};
 return LimitPalmImpulse(state->angle,state->speed,angle,speed,state->dt,l.profile->maxSpeed,l.profile->materialWeight,l.profile->heft,settings.inertia,settings.palmStrength,settings.grabAcceleration);
}
struct Door {
 RE::ObjectRefHandle handle;RE::NiAVObject* root=nullptr;std::vector<Leaf> leaves;std::vector<RE::bhkRigidBody*> backings;
 double closeRetryUntil=0;int state=-1;bool locked=false,teleport=false,activated=false;float npcTime=0,nativeLatchTime=0,cooldown=0,closedTime=0;
};
static std::vector<Door> doors;
static std::unordered_set<RE::FormID> supported;
// Immutable after DataLoaded: a loaded reference keeps the profile assigned to
// its base form even if another plugin later rewrites TESModel::model.
static std::unordered_map<RE::FormID,const ModelProfile*> assignedProfiles;
static std::atomic_uint64_t updateTicks=0,physicsTicks=0,discoveryPasses=0;
static std::mutex stateMutex;
struct ActivationRequest { RE::ObjectRefHandle handle; bool lockpick; };
static std::vector<ActivationRequest> queuedActivation;
static std::atomic_bool menuSuspended=false;
static std::atomic_bool discoveryRequested=true;
static RE::ObjectRefHandle lastInteraction;
static std::atomic_bool physicalActivation=false;
struct HiddenDoor {RE::ObjectRefHandle handle;RE::NiAVObject* root;bool culled;};
static std::vector<HiddenDoor> hiddenDoors;
static void discoveryHeartbeat(const char* source){
 static auto previous=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();
 if(now-previous<std::chrono::seconds(5))return;previous=now;
 std::size_t tracked=0;{std::lock_guard lock(stateMutex);tracked=doors.size();}
 logFile<<"door heartbeat source="<<source<<" ready="<<gameReady.load()<<" suspended="<<menuSuspended.load()
 <<" updates="<<updateTicks.load()<<" physics="<<physicsTicks.load()<<" scans="<<discoveryPasses.load()<<" tracked="<<tracked<<'\n';logFile.flush();
}
static void pollMcmSettings(){
 static auto previous=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();
 if(now-previous<std::chrono::milliseconds(500))return;previous=now;
 std::lock_guard lock(stateMutex);auto old=settings;loadSettings();
 if(old.inertia!=settings.inertia || old.hingeFriction!=settings.hingeFriction || old.angularDamping!=settings.angularDamping || old.grabAcceleration!=settings.grabAcceleration || old.speed!=settings.speed || old.palmStrength!=settings.palmStrength){
  ++tuningRevision;
  logFile<<"MCM physics applied revision="<<tuningRevision<<" inertia="<<settings.inertia<<" damping="<<settings.angularDamping<<" friction="<<settings.hingeFriction<<" speed="<<settings.speed<<" grabAcceleration="<<settings.grabAcceleration<<'\n';logFile.flush();
 }
}
static void restoreHidden(){
 for(auto& d:hiddenDoors)if(auto ref=d.handle.get())if(ref->Get3D()==d.root)d.root->SetAppCulled(d.culled);
 hiddenDoors.clear();
}
struct MenuEvents final:RE::BSTEventSink<RE::MenuOpenCloseEvent>{
 RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* e,RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override{
  if(!e)return RE::BSEventNotifyControl::kContinue;
  if(e->menuName==RE::LoadingMenu::MENU_NAME){
   std::lock_guard guard(stateMutex);
   if(e->opening){menuSuspended.store(true);restoreHidden();doors.clear();queuedActivation.clear();leafPickFilters.store(std::make_shared<const std::unordered_set<std::uint32_t>>());}
   else {menuSuspended.store(false);discoveryRequested.store(true);}
   logFile<<"loading menu="<<e->opening<<"; door tracking reset\n";logFile.flush();
  }else if(e->menuName==RE::LockpickingMenu::MENU_NAME){
   std::lock_guard guard(stateMutex);restoreHidden();menuSuspended.store(e->opening);
   if(e->opening){
    // Use the engine's exact lockpick target; never guess by proximity to a chest or door.
    auto targetOwner=RE::LockpickingMenu::GetTargetReference();auto target=targetOwner.get();
    if(target && (!target->GetBaseObject() || !supported.contains(target->GetBaseObject()->GetFormID())))target=nullptr;
    if(!settings.showDoor && target && target->Get3D()){
     auto root=target->Get3D();hiddenDoors.push_back({target->GetHandle(),root,root->GetAppCulled()});root->SetAppCulled(true);
     logFile<<"lockpick: hidden door="<<std::hex<<target->GetFormID()<<std::dec<<'\n';logFile.flush();
    }
   }
  }
  return RE::BSEventNotifyControl::kContinue;
 }
};
static MenuEvents menuEvents;
static RE::bhkRigidBody* bodyOf(RE::NiAVObject* n){
 if(!n || !n->collisionObject)return nullptr;
 auto c=n->collisionObject->AsBhkNiCollisionObject();
 return c && c->body ? c->body->AsBhkRigidBody():nullptr;
}
static RE::NiAVObject* find(RE::NiAVObject* root,const std::string& name){return root->GetObjectByName(RE::BSFixedString(name.c_str()));}
static void collect(RE::NiAVObject* root,RE::NiAVObject* n,std::vector<Leaf>& leaves,const ModelProfile& model,unsigned depth=0){
 if(!n || depth>64)return;
 std::string name=n->name.c_str()?n->name.c_str():"";
 if(name.starts_with("SVR_Physical_") || name.starts_with("SVR_GrabOnly_")){
  auto anchor=find(root,"SVR_Anchor_"+name.substr(13));
  auto body=bodyOf(n),fixed=bodyOf(anchor);
  if(body && fixed){
   Leaf l;l.body=body;l.anchor=fixed;l.node=n;l.anchorNode=anchor;
   // The original target name is retained in an empty animated node.
   for(const auto& p:model.leaves)if(name==p.leaf){
    l.profile=&p;if(auto s=find(root,p.shadow))l.shadow=s;break;
   }
   if(l.profile)leaves.push_back(std::move(l));
  }
 }
 if(auto node=n->AsNode())for(auto& child:node->GetChildren())collect(root,child.get(),leaves,model,depth+1);
}
static void queue(Door& d,bool lockpick=false){
 if(d.activated || d.cooldown>0)return;
 queuedActivation.push_back({d.handle,lockpick});d.activated=true;d.cooldown=3.f;
}
static float shadowAngle(const Leaf& l,RE::hkpRigidBody* anchor){
 if(!l.shadow)return 1.4f;
 auto& r=l.shadow->world.rotate;
 auto& local=l.profile->perpendicular;
 V p={r.entry[0][0]*local[0]+r.entry[0][1]*local[1]+r.entry[0][2]*local[2],r.entry[1][0]*local[0]+r.entry[1][1]*local[1]+r.entry[1][2]*local[2],r.entry[2][0]*local[0]+r.entry[2][1]*local[1]+r.entry[2][2]*local[2]};
 auto& a=l.profile->axis;auto axis=unit(rotate(anchor,{a[0],a[1],a[2]})),closed=rotate(anchor,{local[0],local[1],local[2]});
 p=unit(add(p,scale(axis,-dot(axis,p))));
 return std::atan2(dot(axis,cross(closed,p)),dot(closed,p));
}
static bool handNear(RE::bhkRigidBody* wrapper){
 auto api=HiggsAPI::g_higgs;if(!api)return false;
 RE::hkAabb aabb;wrapper->GetAabbWorldspace(aabb);
 auto lo=read(aabb.min),hi=read(aabb.max);
 for(bool left:{false,true})if(auto object=api->GetHandRigidBody(left))if(auto hand=object->AsBhkRigidBody())if(auto b=hand->GetRigidBody()){
  auto contacts=palmContacts.load();auto state=contacts->find(wrapper->GetRigidBody());
  if(state!=contacts->end() && state->second->uid==wrapper->GetRigidBody()->uid && (state->second->ignoreHands.load()&(1u<<unsigned(left))))continue;
  auto p=read(b->motion.motionState.transform.translation);
  if(p.x>=lo.x-.18f && p.x<=hi.x+.18f && p.y>=lo.y-.18f && p.y<=hi.y+.18f && p.z>=lo.z-.18f && p.z<=hi.z+.18f)return true;
 }
 return false;
}
// Skyrim VR GetNodeFromCollidable (E01FE0), used by both native picking and
// HIGGS, reads hkp property 2 -> bhkNiCollisionObject -> sceneObject. hk userData
// alone is NOT this link. Some engine-created bodies lose/omit property 2.
static void repairSelectionLink(Leaf& l,RE::TESObjectREFR* ref){
 if(!l.node || !l.body)return;
 auto collision=l.node->collisionObject?l.node->collisionObject->AsBhkNiCollisionObject():nullptr;
 auto leaf=l.body->GetRigidBody();
 if(!collision || collision->sceneObject!=l.node || collision->body.get()!=l.body || !leaf)return;
 auto current=leaf->GetProperty(2);
 if(current && current->value.GetPointer()==collision)return;
 static REL::Relocation<void(*)(RE::hkpWorldObject*,std::uint32_t,RE::hkpPropertyValue)> setProperty{REL::Offset(0xAB9AE0)};
 setProperty(leaf,2,RE::hkpPropertyValue(static_cast<void*>(collision)));
 // Verify through the SAME game resolver used by HIGGS, not only node ancestry.
 static REL::Relocation<RE::TESObjectREFR*(*)(const RE::hkpCollidable*)> resolve{REL::Offset(0x3B4940)};
 auto resolved=resolve(&leaf->collidable);
 logFile<<"selection link repaired ref="<<std::hex<<ref->GetFormID()<<std::dec<<" leaf="<<l.node->name.c_str()<<" resolved="<<(resolved==ref)<<'\n';logFile.flush();
}
static void repairSelectionLinks(){
 if(!gameReady.load() || menuSuspended.load())return;
 auto player=RE::PlayerCharacter::GetSingleton();auto cell=player?player->GetParentCell():nullptr;
 auto world=cell?cell->GetbhkWorld():nullptr;if(!world)return;
 std::lock_guard guard(stateMutex);if(doors.empty())return;
 RE::BSWriteLockGuard worldGuard(world->worldLock);
 for(auto& d:doors){
  auto ref=d.handle.get();if(!ref || !ref->loadedData || ref->loadedData->data3D.get()!=d.root)continue;
  for(auto& l:d.leaves)if(auto leaf=l.body->GetRigidBody();leaf && leaf->world==world->GetWorld1())repairSelectionLink(l,ref.get());
 }
}
static float capsuleMassThreshold=69.f;
static float configuredCapsuleMass(HiggsAPI::IHiggsInterface001* api){
 double value=0;
 if(api && api->GetSettingDouble("minCollideClutterMass",value) && std::isfinite(value) && value>=0 && value<=10000)return float(value);
 char text[64]{};
 GetPrivateProfileStringA("Settings","minCollideClutterMass","69",text,sizeof(text),".\\Data\\SKSE\\Plugins\\higgs_vr.ini");
 char* end=nullptr;value=std::strtod(text,&end);
 return end!=text && std::isfinite(value) && value>=0 && value<=10000?float(value):69.f;
}
static void ensureCapsuleMass(Door& d,Leaf& l,bool held){
 auto leaf=l.body->GetRigidBody();if(!leaf)return;
 auto motion=*leaf->motion.type;
 if(motion==RE::hkpMotion::MotionType::kFixed || motion==RE::hkpMotion::MotionType::kKeyframed)return;
 float current=leaf->motion.GetMass();float target=DoorCapsuleMass(current,capsuleMassThreshold,d.teleport,held);
 if(target>current+.0001f){
  // Native hkpMotion::SetMass only writes inverse linear mass (motion+DC).
  // Keep inverse rotational inertia XYZ unchanged: opening torque/hinge friction
  // do not acquire the much heavier inertia of a regenerated heavy door mesh.
  leaf->motion.SetMass(target);
  if(auto ref=d.handle.get()){logFile<<"player capsule mass ref="<<std::hex<<ref->GetFormID()<<std::dec<<" leaf="<<l.node->name.c_str()<<" old="<<current<<" new="<<leaf->motion.GetMass()<<" threshold="<<capsuleMassThreshold<<" rotational inertia unchanged\n";logFile.flush();}
 }
}
static void physics(void* w){
 ++physicsTicks;
 auto api=HiggsAPI::g_higgs;if(!api || !w || !gameReady.load() || menuSuspended.load())return;
 auto world=static_cast<RE::bhkWorld*>(w);
 static auto previous=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();
 float dt=std::clamp(std::chrono::duration<float>(now-previous).count(),.001f,.03f);previous=now;
 std::lock_guard guard(stateMutex);if(doors.empty())return;
 double currentThreshold=0;
 if(api->GetSettingDouble("minCollideClutterMass",currentThreshold) && std::isfinite(currentThreshold) && currentThreshold>=0 && currentThreshold<=10000)capsuleMassThreshold=float(currentThreshold);
 phase.store("physics: world lock");RE::BSWriteLockGuard worldGuard(world->worldLock);
 auto pickFilters=std::make_shared<std::unordered_set<std::uint32_t>>();
 auto grilles=std::make_shared<std::unordered_set<std::uint32_t>>();
 auto palmSnapshot=std::make_shared<PalmContacts>();const auto palmSerial=++palmStep;
 for(unsigned i=0;i<2;++i){auto n=api->GetHandRigidBody(i!=0);auto b=n?n->AsBhkRigidBody():nullptr;palmBodies[i].store(b?b->GetRigidBody():nullptr);}
 for(unsigned i=0;i<2;++i)palmBodies[i+2].store(pcvrBareHandGetter?pcvrBareHandGetter(i!=0):nullptr);
 float palmDt=world->GetWorld1()->dynamicsStepInfo.stepInfo.deltaTime;
 if(!std::isfinite(palmDt) || palmDt<.001f || palmDt>.05f)palmDt=dt;
 for(auto& d:doors){
  auto ref=d.handle.get();if(!ref || !ref->loadedData || ref->loadedData->data3D.get()!=d.root)continue;
  bool held=api->GetGrabbedObject(true)==ref.get() || api->GetGrabbedObject(false)==ref.get();
  // A recent NPC animation must not pull a leaf back while the player pushes it.
  // HIGGS grab and bare-hand contact both take precedence over the NPC servo.
  if(held || (d.teleport && std::any_of(d.leaves.begin(),d.leaves.end(),[](const Leaf& l){return handNear(l.body);})))d.npcTime=0;
  for(auto wrapper:d.backings)if(auto backing=wrapper->GetRigidBody();backing && backing->world==world->GetWorld1()){
   if(!registeredBackings.contains(backing) || registeredBackings[backing]!=backing->uid){
    backing->contactPointCallbackDelay=0;backing->AddContactListener(&portalBackingContacts);registeredBackings[backing]=backing->uid;
    if(auto filter=RE::bhkCollisionFilter::GetSingleton()){
     auto& info=backing->collidable.broadPhaseHandle.collisionFilterInfo.filter;
     info=(info&0xffffu)|(filter->GetNewSystemGroup()<<16);
     auto next=std::make_shared<std::unordered_set<std::uint32_t>>(*portalFilters.load());next->insert(info);portalFilters.store(next,std::memory_order_release);
    }
   }
  }
  d.npcTime=(std::max)(0.f,d.npcTime-dt);d.nativeLatchTime=(std::max)(0.f,d.nativeLatchTime-dt);d.cooldown=(std::max)(0.f,d.cooldown-dt);
  float maxAngle=0,maxSpeed=0;
  for(auto& l:d.leaves){
   auto leaf=l.body->GetRigidBody(),anchor=l.anchor->GetRigidBody();
   if(!leaf || !anchor || leaf->world!=world->GetWorld1() || anchor->world!=leaf->world)continue;
   repairSelectionLink(l,ref.get());
   bool partHeld=held && (api->GetGrabbedRigidBody(true)==l.body || api->GetGrabbedRigidBody(false)==l.body);
   // Native door activation may keyframe collision branches. Restore only OUR
   // generated leaf using the engine API, never by assigning a motion enum/vtable.
   unsigned motion=static_cast<unsigned>(*leaf->motion.type);
   if(DoorNeedsDynamicRepair(motion,partHeld)){
    static REL::Relocation<void(*)(RE::bhkRigidBody*,std::uint32_t,std::uint32_t,std::uint32_t)> setMotion{REL::Offset(0xE08040)};
    setMotion(l.body,1,1,0); // dynamic, activate, full collision-filter update (Skyrim VR)
    leaf->collidable.broadPhaseHandle.objectQualityType=5;
    logFile<<"selection repaired ref="<<std::hex<<ref->GetFormID()<<std::dec<<" oldMotion="<<motion<<" newMotion="<<static_cast<unsigned>(*leaf->motion.type)<<'\n';logFile.flush();
   }
   if(l.node && l.node->GetUserData()!=ref.get())l.node->SetUserData(ref.get());
   if(!leaf->userData)leaf->userData=reinterpret_cast<std::uintptr_t>(l.body);
   if(!partHeld)if(auto filter=RE::bhkCollisionFilter::GetSingleton()){
    auto& info=leaf->collidable.broadPhaseHandle.collisionFilterInfo.filter;
    if(!l.pickGroup)l.pickGroup=filter->GetNewSystemGroup();
    auto desiredInfo=(info&0xff80u)|4u|(l.pickGroup<<16);
    if(l.pickGroup && info!=desiredInfo){
     info=desiredInfo;
     static REL::Relocation<void(*)(RE::bhkRigidBody*,std::uint32_t,std::uint32_t,std::uint32_t)> refresh{REL::Offset(0xE08040)};
     refresh(l.body,1,1,0);leaf->collidable.broadPhaseHandle.objectQualityType=5;
    }
    if(l.pickGroup){pickFilters->insert(info);if(l.profile && std::string_view(l.profile->leaf).starts_with("SVR_GrabOnly_"))grilles->insert(info);}
   }
   ensureCapsuleMass(d,l,partHeld);
   if(!registeredContacts.contains(leaf) || registeredContacts[leaf]!=leaf->uid){
    leaf->contactPointCallbackDelay=0;leaf->AddContactListener(&doorContacts);registeredContacts[leaf]=leaf->uid;l.contactBody=leaf;l.contactUid=leaf->uid;
    // Only these generated leaves: tighter contact tolerance, not global HIGGS settings.
    leaf->collidable.allowedPenetrationDepth=.0025f;
    logFile<<"door selection ref="<<std::hex<<ref->GetFormID()<<std::dec<<" motion="<<static_cast<unsigned>(*leaf->motion.type)<<" layer="<<(leaf->collidable.broadPhaseHandle.collisionFilterInfo.filter&0x7f)<<" owner="<<(l.node && l.node->GetUserData()==ref.get())<<" teleport="<<d.teleport<<'\n';logFile.flush();
   }
   if(!l.profile)continue;
   if(!l.palm || l.palm->uid!=leaf->uid){l.palm=std::make_shared<PalmContactState>(leaf->uid,ref->GetFormID());l.palm->permeable=std::string_view(l.profile->leaf).starts_with("SVR_GrabOnly_");}
   palmSnapshot->emplace(leaf,l.palm);
   unsigned heldMask=0;
   for(unsigned i=0;i<2;++i)if(api->GetGrabbedRigidBody(i!=0)==l.body)heldMask|=1u<<i;
   const double releaseNow=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   unsigned ignore=0;
   for(unsigned i=0;i<2;++i){
    const bool heldNow=(heldMask&(1u<<i))!=0;
    double until=l.palm->releaseUntil[i].load();
    // Polling is a fallback; the HIGGS drop callback arms protection immediately.
    if((l.palm->heldHands&(1u<<i)) && !heldNow && until<=releaseNow){
     until=DoorReleaseDeadline(releaseNow);l.palm->releaseUntil[i].store(until);l.palm->contact.store(0);
    }
    if(heldNow){until=0;l.palm->releaseUntil[i].store(0);}
    if(DoorReleaseBlocked(until,releaseNow,heldNow))ignore|=1u<<i;
   }
   l.palm->heldHands=heldMask;l.palm->ignoreHands.store(ignore);
   if(!partHeld)leaf->motion.motionState.angularDamping=settings.angularDamping;
   if(l.tunedUid!=leaf->uid || l.tunedRevision!=tuningRevision){
    leaf->motion.motionState.angularDamping=settings.angularDamping;
    auto& inertia=leaf->motion.inertiaAndMassInv.quad;
    auto [baseline,inserted]=inertiaBaselines.try_emplace(leaf,InertiaBaseline{leaf->uid,{inertia.m128_f32[0],inertia.m128_f32[1],inertia.m128_f32[2]}});
    if(!inserted && baseline->second.uid!=leaf->uid)baseline->second={leaf->uid,{inertia.m128_f32[0],inertia.m128_f32[1],inertia.m128_f32[2]}};
    for(unsigned i=0;i<3;++i)inertia.m128_f32[i]=baseline->second.inverse[i]/DoorNativeInertiaScale(settings.inertia,l.profile->heft);
    auto tuneConstraint=[&](RE::hkpConstraintInstance* c){if(c && c->data && c->data->GetType()==RE::hkpConstraintData::kLimitedHinge)
      static_cast<RE::hkpLimitedHingeConstraintData*>(c->data)->atoms.angFriction.maxFrictionTorque=settings.hingeFriction;};
    for(auto c:leaf->constraintsSlave)tuneConstraint(c);
    for(auto c:anchor->constraintsSlave)tuneConstraint(c);
    l.tunedUid=leaf->uid;l.tunedRevision=tuningRevision;
   }
   auto& a=l.profile->axis;auto& p=l.profile->perpendicular;
   auto axis=unit(rotate(anchor,{a[0],a[1],a[2]}));auto current=rotate(leaf,{p[0],p[1],p[2]});
   current=unit(add(current,scale(axis,-dot(axis,current))));auto closed=rotate(anchor,{p[0],p[1],p[2]});
   float angle=std::atan2(dot(axis,cross(closed,current)),dot(closed,current));
   float omega=dot(read(leaf->motion.angularVelocity),axis);
   auto palm=TakePalmResponse(l,angle,omega,partHeld);
   const bool palmAdjusted=palm.angle!=angle;angle=palm.angle;omega=palm.speed;
   bool lockedRotation=d.locked && std::abs(angle)>.0002f;
   bool initialize=!l.poseInitialized || l.poseUid!=leaf->uid;
   float limited=d.locked?0.f:std::clamp(initialize?(d.teleport?0.f:shadowAngle(l,anchor)):angle,l.profile->oneWay?0.f:-l.profile->maxAngle,l.profile->maxAngle);
   if(!initialize && std::abs(axis.z)<.3f && !d.locked && d.npcTime<=0 && std::string_view(l.profile->shadow)=="Lid01"){
    auto result=l.lid.Step(limited,omega,l.profile->maxAngle,partHeld,handNear(l.body),dt);limited=result.angle;omega=result.speed;if(result.closed)l.latch.latched=true;
   }
   if(initialize){l.momentum.reset();l.latch=DoorLatch{};if(std::abs(limited)>.05235988f){l.latch.latched=false;l.latch.canLatch=true;}}
   bool freshGrab=partHeld && !l.latch.wasHeld;
   bool latched=l.latch.Update(limited,omega,partHeld,d.nativeLatchTime>0,d.locked);
   // A failed/delayed transition must not leave a portal unusable while open.
   // A deliberate new grab after the debounce can retry without closing it first.
   if(d.teleport && freshGrab && !d.locked && d.cooldown<=0)d.activated=false;
   if(latched)limited=0;
   bool exceeded=limited!=angle;
   auto turn=[&](V v){return add(add(scale(v,std::cos(limited)),scale(cross(axis,v),std::sin(limited))),scale(axis,dot(axis,v)*(1-std::cos(limited))));};
   auto x=turn(rotate(anchor,{1,0,0})),y=turn(rotate(anchor,{0,1,0})),z=turn(rotate(anchor,{0,0,1}));
   angle=limited;if(d.locked || latched || exceeded)omega=0;
   auto position=read(anchor->motion.motionState.transform.translation);
   auto error=add(position,scale(read(leaf->motion.motionState.transform.translation),-1));
   bool displaced=dot(error,error)>.00000025f || dot(unit(rotate(leaf,{a[0],a[1],a[2]})),axis)<.999998f;
   if(lockedRotation || initialize || displaced || exceeded || palmAdjusted){
    auto pos=packed(position);auto q=quaternion(x,y,z);l.body->SetPositionAndRotation(pos,q);
    if(initialize){leaf->SetLinearVelocity(packed({0,0,0}));leaf->SetAngularVelocity(packed({0,0,0}));omega=0;l.commandedSpeed=0;l.poseInitialized=true;l.poseUid=leaf->uid;}
   }
   float desired=omega;
   if(d.locked || latched)desired=0;
   else if(!held && d.npcTime>0){desired=d.teleport?2.f*(shadowAngle(l,anchor)-angle):6.f*(NativeDoorTarget(d.state,shadowAngle(l,anchor),l.profile->maxAngle)-angle);}
   // The player never gets an automatic return spring. Limit edge speed and acceleration instead.
   desired=DoorAngularSpeed(desired,l.profile->maxSpeed,settings.speed);
   // Only NPC animation following is a velocity servo. A free door keeps
   // the solver's actual velocity; never drag it toward an older command.
   omega=(d.locked || latched)?0.f:DoorDrivenAngularSpeed(omega,l.commandedSpeed,desired,dt,d.npcTime>0,l.profile->maxSpeed*settings.speed);
   if(!d.teleport && !held && !d.locked && !latched && d.npcTime>0)omega=std::clamp(desired,-3.f,3.f);
   l.commandedSpeed=omega;
   l.palm->angle=angle;l.palm->speed=omega;l.palm->dt=palmDt;l.palm->prepared.store(palmSerial);
   auto angular=scale(axis,omega);auto com=read(leaf->motion.motionState.sweptTransform.centerOfMassLocal);
   auto radius=add(add(scale(x,com.x),scale(y,com.y)),scale(z,com.z));
   // Do not wake every resting body by writing unchanged velocities each frame.
   auto angularError=add(angular,scale(read(leaf->motion.angularVelocity),-1));
   auto linear=cross(angular,radius);auto linearError=add(linear,scale(read(leaf->motion.linearVelocity),-1));
   if(dot(angularError,angularError)>1e-8f)leaf->SetAngularVelocity(packed(angular));
   if(dot(linearError,linearError)>1e-8f)leaf->SetLinearVelocity(packed(linear));
   l.angle=angle;maxAngle=(std::max)(maxAngle,std::abs(angle));maxSpeed=(std::max)(maxSpeed,std::abs(omega));
   bool touched=held || handNear(l.body);
   // Locked doors require an actual grab or native activation, never palm proximity.
   if(!d.locked && touched && d.npcTime==0 && (d.teleport?std::abs(angle)>.20943951f:DoorOpeningMotion(angle,omega)) && (d.teleport || d.state==3 || d.state==0))queue(d);
  }
  d.closedTime=DoorRestingClosed(maxAngle,maxSpeed)?d.closedTime+dt:0.f;
  if(d.closedTime>.25f && d.cooldown<=0)d.activated=false;
 }
 leafPickFilters.store(pickFilters,std::memory_order_release);grilleFilters.store(grilles);
 palmContacts.store(palmSnapshot);
 static auto reported=std::chrono::steady_clock::now();
 if(now-reported>std::chrono::seconds(10)){
  logFile<<"suppressed door/world contacts="<<disabledDoorContacts.load()<<'\n';logFile.flush();reported=now;
 }
 phase.store("idle after physics");
}
static void releaseGraceEvent(bool left,RE::TESObjectREFR* ref,bool isGrab){
 if(!ref)return;
 auto snapshot=palmContacts.load();const unsigned hand=unsigned(left),bit=1u<<hand;
 const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 for(const auto& [body,state]:*snapshot)if(state->refID==ref->GetFormID()){
  if(isGrab){state->releaseUntil[hand].store(0);state->ignoreHands.fetch_and(~bit);}
  else{state->releaseUntil[hand].store(DoorReleaseDeadline(now));state->ignoreHands.fetch_or(bit);state->contact.store(0);state->palmSeen[hand].store(0);state->palmSince[hand].store(0);}
 }
}
static void dropped(bool left,RE::TESObjectREFR* ref){releaseGraceEvent(left,ref,false);}
static void grabbed(bool left,RE::TESObjectREFR* ref){
 if(!ref)return;releaseGraceEvent(left,ref,true);std::lock_guard guard(stateMutex);
 for(auto& d:doors)if(d.handle==ref->GetHandle() && !d.teleport){d.npcTime=0;d.nativeLatchTime=0;d.closeRetryUntil=0;}
 for(auto& d:doors)if(d.handle==ref->GetHandle() && DoorLockpickIntent(d.locked,true,settings.lockpickOnGrab)){queue(d,true);break;}
}
static void enforceAnchors(){
 auto api=HiggsAPI::g_higgs;if(!api)return;
 if(!gameReady.load() || menuSuspended.load())return;
 auto player=RE::PlayerCharacter::GetSingleton();auto cell=player?player->GetParentCell():nullptr;
 auto world=cell?cell->GetbhkWorld():nullptr;if(!world)return;
 std::lock_guard guard(stateMutex);if(doors.empty())return;
 RE::BSWriteLockGuard worldGuard(world->worldLock);
 for(auto& d:doors){
  auto ref=d.handle.get();if(!ref || !ref->loadedData || ref->loadedData->data3D.get()!=d.root)continue;
  for(auto& l:d.leaves){
   auto leaf=l.body->GetRigidBody(),anchor=l.anchor->GetRigidBody();
   if(!l.profile || !l.poseInitialized || !leaf || !anchor || leaf->uid!=l.poseUid || leaf->world!=world->GetWorld1() || anchor->world!=leaf->world)continue;
   auto& a=l.profile->axis;auto& p=l.profile->perpendicular;
   auto pose=ProjectHinge(read(anchor->motion.motionState.transform.translation),rotate(anchor,{1,0,0}),rotate(anchor,{0,1,0}),rotate(anchor,{0,0,1}),V{a[0],a[1],a[2]},V{p[0],p[1],p[2]},rotate(leaf,{p[0],p[1],p[2]}),l.angle,l.profile->maxAngle,d.locked || l.latch.latched,l.profile->oneWay?0.f:-l.profile->maxAngle);
   const bool heldLeaf=api->GetGrabbedRigidBody(true)==l.body || api->GetGrabbedRigidBody(false)==l.body;
   const double responseNow=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   if(heldLeaf && l.momentum.sampled && !d.locked && !l.latch.latched){
    const float dt=float(responseNow-l.momentum.time);
    if(dt>.001f && dt<.1f){
     auto response=DoorHeldResponse(l.momentum.angle,l.momentum.lastSpeed,pose.angle,dt,l.profile->maxSpeed,l.profile->materialWeight,settings.grabAcceleration,settings.speed,settings.inertia,l.profile->heft);
     const float angle=response.angle;
     auto closed=rotate(anchor,{p[0],p[1],p[2]});
     auto direction=add(scale(closed,std::cos(angle)),scale(cross(pose.axis,closed),std::sin(angle)));
     pose=ProjectHinge(pose.position,rotate(anchor,{1,0,0}),rotate(anchor,{0,1,0}),rotate(anchor,{0,0,1}),V{a[0],a[1],a[2]},V{p[0],p[1],p[2]},direction,angle,l.profile->maxAngle,false,l.profile->oneWay?0.f:-l.profile->maxAngle);
    }
   }
   auto error=add(pose.position,scale(read(leaf->motion.motionState.transform.translation),-1));
   float beforeProjection=dot(read(leaf->motion.angularVelocity),pose.axis);
   auto palm=TakePalmResponse(l,pose.angle,beforeProjection,heldLeaf);beforeProjection=palm.speed;
   if(palm.angle!=pose.angle){
    auto closed=rotate(anchor,{p[0],p[1],p[2]});
    auto direction=add(scale(closed,std::cos(palm.angle)),scale(cross(pose.axis,closed),std::sin(palm.angle)));
    pose=ProjectHinge(pose.position,rotate(anchor,{1,0,0}),rotate(anchor,{0,1,0}),rotate(anchor,{0,0,1}),V{a[0],a[1],a[2]},V{p[0],p[1],p[2]},direction,palm.angle,l.profile->maxAngle,d.locked || l.latch.latched,l.profile->oneWay?0.f:-l.profile->maxAngle);
   }
   bool moved=dot(error,error)>.00000025f || dot(rotate(leaf,{1,0,0}),pose.x)<.999998f || dot(rotate(leaf,{0,1,0}),pose.y)<.999998f || dot(rotate(leaf,{0,0,1}),pose.z)<.999998f;
   if(moved){
    auto position=packed(pose.position);auto q=quaternion(pose.x,pose.y,pose.z);l.body->SetPositionAndRotation(position,q);
    // Synchronize only this leaf's visual branch in the same frame; do not leave
    // a one-frame lifted mesh after the physics body has already been corrected.
    if(l.node && l.anchorNode){
     auto visual=l.anchorNode->world;
     visual.rotate.entry[0][0]=pose.x.x;visual.rotate.entry[1][0]=pose.x.y;visual.rotate.entry[2][0]=pose.x.z;
     visual.rotate.entry[0][1]=pose.y.x;visual.rotate.entry[1][1]=pose.y.y;visual.rotate.entry[2][1]=pose.y.z;
     visual.rotate.entry[0][2]=pose.z.x;visual.rotate.entry[1][2]=pose.z.y;visual.rotate.entry[2][2]=pose.z.z;
     l.node->local=l.node->parent?l.node->parent->world.Invert()*visual:visual;
     RE::NiUpdateData data{};data.flags.set(RE::NiUpdateData::Flag::kDisableCollision);l.node->UpdateDownwardPass(data,0);
    }
   }
   const bool touched=api->GetGrabbedObject(true)==ref.get() || api->GetGrabbedObject(false)==ref.get() || handNear(l.body);
   const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   const bool blocked=d.locked || l.latch.latched || d.npcTime>0;
   const bool grabbed=api->GetGrabbedRigidBody(true)==l.body || api->GetGrabbedRigidBody(false)==l.body;
   const bool releasing=(l.momentum.wasHeld&&!grabbed)||(l.momentum.wasTouched&&!touched);
   const bool releasedGrab=l.momentum.wasHeld;
   const float measuredBeforeRelease=l.momentum.lastSpeed;
   float omega=l.momentum.Step(now,pose.angle,beforeProjection,touched,blocked,l.profile->maxSpeed*settings.speed,grabbed);
   if((pose.angle>=l.profile->maxAngle && omega>0)||(pose.angle<=(l.profile->oneWay?0.f:-l.profile->maxAngle) && omega<0))omega=0;
   if(releasing){logFile<<"door release ref="<<std::hex<<ref->GetFormID()<<std::dec<<" kind="<<(releasedGrab?"grab":"palm")<<" poseSpeed="<<measuredBeforeRelease<<" incoming="<<beforeProjection<<" applied="<<omega<<" cap="<<l.profile->maxSpeed*settings.speed<<" blocked="<<blocked<<'\n';logFile.flush();}
   auto angular=scale(pose.axis,omega),com=read(leaf->motion.motionState.sweptTransform.centerOfMassLocal);
   auto radius=add(add(scale(pose.x,com.x),scale(pose.y,com.y)),scale(pose.z,com.z));auto linear=cross(angular,radius);
   auto angularError=add(angular,scale(read(leaf->motion.angularVelocity),-1)),linearError=add(linear,scale(read(leaf->motion.linearVelocity),-1));
   if(dot(angularError,angularError)>1e-8f)leaf->SetAngularVelocity(packed(angular));
   if(dot(linearError,linearError)>1e-8f)leaf->SetLinearVelocity(packed(linear));
   l.angle=pose.angle;l.commandedSpeed=omega;
  }
 }
}
static bool activatorsOwnRiftenStall=false;
static bool delegatedDoor(const ModelProfile& model){
 return activatorsOwnRiftenStall && std::string_view(model.original)=="architecture\\riften\\rtmarketstalldoor01.nif";
}
static void update(){
 ++updateTicks;pollMcmSettings();discoveryHeartbeat("post-higgs");
 if(!gameReady.load())return;
 phase.store("update: player");
 auto player=RE::PlayerCharacter::GetSingleton();auto tes=RE::TES::GetSingleton();
 if(!player || !tes || !player->GetParentCell() || !player->Get3D())return;
 if(auto ui=RE::UI::GetSingleton();ui && ui->GameIsPaused())return;
 std::vector<ActivationRequest> activate;
 std::vector<RE::ObjectRefHandle> close,npcOpen;
 {
  std::lock_guard guard(stateMutex);activate.swap(queuedActivation);
  for(auto& d:doors)if(auto ref=d.handle.get();ref && ref->Get3D()==d.root){
   int state=int(RE::BGSOpenCloseForm::GetOpenState(ref.get()));
   d.locked=ref->IsLocked() || (ref->IsActivationBlocked() && state!=1 && state!=2);
   if(NativeDoorTransition(d.teleport,d.state,state,d.activated)){
    d.npcTime=5.f;d.nativeLatchTime=5.f;
    for(auto& leaf:d.leaves)leaf.latch.latched=false;
   }
   d.state=state;
   bool npcContact=false;
   for(auto& l:d.leaves)if(l.palm){auto id=l.palm->actorContact.exchange(0);if(id){auto actor=RE::TESForm::LookupByID<RE::Actor>(id);npcContact|=actor && !actor->IsDead() && actor->Get3D() && actor->GetParentCell()==ref->GetParentCell();}}
   float angle=0;for(auto& l:d.leaves)angle=(std::max)(angle,std::abs(l.angle));
   auto api=HiggsAPI::g_higgs;
   bool held=api && (api->GetGrabbedObject(true)==ref.get() || api->GetGrabbedObject(false)==ref.get());
   if(npcContact && !d.teleport && !d.locked && !held && d.npcTime<=0){npcOpen.push_back(d.handle);d.npcTime=3;d.nativeLatchTime=3;for(auto& l:d.leaves)l.latch.latched=false;}
   bool latched=!d.leaves.empty() && std::all_of(d.leaves.begin(),d.leaves.end(),[](const Leaf& l){return l.poseInitialized && l.latch.latched;});
   const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   if(angle<.035f && DoorNeedsCloseCommit(d.teleport,d.locked,state,held,latched,d.closedTime,d.npcTime,now,d.closeRetryUntil)){
    close.push_back(d.handle);d.closeRetryUntil=now+.5;
   }
  }
 }
 // A controller pressing into a physical leaf may not produce TESActivateEvent.
 // Request a native OPEN (never a toggle) so an already-open logical door cannot close on the actor.
 for(auto& h:npcOpen)if(auto ref=h.get();ref && ref->Get3D() && !ref->IsLocked() && !ref->IsActivationBlocked()){
  RE::BGSOpenCloseForm::SetOpenState(ref.get(),true,false);
  std::lock_guard guard(stateMutex);for(auto& d:doors)if(d.handle==h){d.state=1;d.activated=false;break;}
  logFile<<"NPC physical door request ref="<<std::hex<<ref->GetFormID()<<std::dec<<'\n';logFile.flush();
 }
 // Native events may call back into this plugin: never invoke them with stateMutex held.
 for(auto& h:close)if(auto ref=h.get();ref && ref->Get3D()){
  // Engine call may dispatch events: keep it outside stateMutex/world locks.
  RE::BGSOpenCloseForm::SetOpenState(ref.get(),false,true);
  const int actual=int(RE::BGSOpenCloseForm::GetOpenState(ref.get()));
  std::lock_guard guard(stateMutex);
  for(auto& d:doors)if(d.handle==h && d.root==ref->Get3D()){
   d.state=actual;
   if(actual==3){d.activated=false;d.nativeLatchTime=0;}
   logFile<<"physical close ref="<<std::hex<<ref->GetFormID()<<std::dec<<" confirmed="<<(actual==3)<<" nativeState="<<actual<<'\n';logFile.flush();break;
  }
 }
 // Engine activation and lockpick/teleport actions run outside the physics world lock.
 for(const auto& request:activate)if(auto ref=request.handle.get()){
  if(!ref->Get3D() || ref->IsLocked()!=request.lockpick)continue;
  if(request.lockpick){
   auto api=HiggsAPI::g_higgs;
   if(!api || (api->GetGrabbedObject(true)!=ref.get() && api->GetGrabbedObject(false)!=ref.get()))continue;
  }
  const auto& h=request.handle;
  lastInteraction=h;
  // ActivateRef may start an animation/delay before LoadingMenu actually opens.
  // Keep pose/velocity/activation state during that delay. LoadingMenu's opening
  // callback releases tracking before cell removal; unloaded references are skipped.
  phase.store("native door activation");
  physicalActivation.store(true);
  bool success=ref->ActivateRef(player,0,nullptr,1,false);
  physicalActivation.store(false);
  phase.store("after native door activation");
  if(!success && !ref->IsLocked()){
   std::lock_guard guard(stateMutex);
   for(auto& d:doors)if(d.handle==h){d.activated=false;d.cooldown=.75f;break;}
  }
  logFile<<"activate ref="<<std::hex<<ref->GetFormID()<<std::dec<<" reason="<<(request.lockpick?"grab-lockpick":"opening-motion")<<" locked="<<ref->IsLocked()<<" accepted="<<success<<'\n';logFile.flush();
 }
 // Scan immediately after loading; keep the existing amortized cadence otherwise.
 static unsigned frame=0;if(++frame%6 && !discoveryRequested.exchange(false))return;
 std::vector<Door> discovered;
 ++discoveryPasses;unsigned visited=0,missingPairs=0;
 phase.store("update: discovering doors");
 tes->ForEachReference([&](RE::TESObjectREFR* ref){
  auto base=ref?ref->GetBaseObject():nullptr;
  if(!base || !base->As<RE::TESObjectDOOR>() || !ref->loadedData || !ref->loadedData->data3D)return RE::BSContainer::ForEachResult::kContinue;
  // A runtime base/model swap can occur after DataLoaded (BOS and similar).
  // Recognize only our exact generated paths, never guess from a filename suffix.
  auto assigned=AssignedDoorProfile(assignedProfiles,base->GetFormID());
  if(!assigned){
   auto door=base->As<RE::TESObjectDOOR>();
   auto name=NormalizeDoorModel(door->GetModel()?door->GetModel():"");
   for(const auto& model:MODELS)if(name==model.physical){assigned=&model;break;}
  }
  if(!assigned || delegatedDoor(*assigned))return RE::BSContainer::ForEachResult::kContinue;
  Door d;d.handle=ref->GetHandle();d.root=ref->loadedData->data3D.get();d.locked=ref->IsLocked() || ref->IsActivationBlocked();
  d.teleport=ref->extraList.GetByType<RE::ExtraTeleport>()!=nullptr;
  ++visited;
  if(assigned){
   const auto& model=*assigned;
   collect(d.root,d.root,d.leaves,model);
   if(d.teleport)for(auto name:model.portalBackings)if(auto backing=bodyOf(find(d.root,name)))d.backings.push_back(backing);
  }
  if(!d.leaves.empty())discovered.push_back(std::move(d));
  else if(missingPairs++<2 && discoveryPasses.load()%300==1){
   auto door=base->As<RE::TESObjectDOOR>();
   logFile<<"door missing generated leaf/anchor pair ref="<<std::hex<<ref->GetFormID()<<std::dec<<" base="<<std::hex<<base->GetFormID()<<std::dec<<" model="<<(door&&door->GetModel()?door->GetModel():"<null>")<<'\n';logFile.flush();
  }
  return RE::BSContainer::ForEachResult::kContinue;
 });
 std::lock_guard guard(stateMutex);
 for(auto& d:discovered)for(auto& old:doors)if(old.handle==d.handle && old.root==d.root){
  d.closeRetryUntil=old.closeRetryUntil;d.state=old.state;d.npcTime=old.npcTime;d.nativeLatchTime=old.nativeLatchTime;d.cooldown=old.cooldown;d.activated=old.activated;d.closedTime=old.closedTime;
  for(auto& leaf:d.leaves)for(auto& previous:old.leaves)if(leaf.body==previous.body){leaf.lid=previous.lid;leaf.latch=previous.latch;leaf.pickGroup=previous.pickGroup;leaf.poseUid=previous.poseUid;leaf.poseInitialized=previous.poseInitialized;leaf.angle=previous.angle;leaf.commandedSpeed=previous.commandedSpeed;leaf.contactBody=previous.contactBody;leaf.contactUid=previous.contactUid;leaf.tunedUid=previous.tunedUid;leaf.tunedRevision=previous.tunedRevision;leaf.momentum=previous.momentum;leaf.palm=previous.palm;break;}
  break;
 }
 if(discovered.size()!=doors.size() || discoveryPasses.load()%300==1){logFile<<"door discovery visited="<<visited<<" missing="<<missingPairs<<"; loaded physical doors="<<discovered.size()<<'\n';logFile.flush();}
 // Loaded persistent references can temporarily fall outside the TES iteration.
 // Do not abandon their dynamic leaves while the same scene root remains loaded.
 for(auto& old:doors){
  auto ref=old.handle.get();
  if(!ref || !ref->loadedData || ref->loadedData->data3D.get()!=old.root)continue;
  if(std::none_of(discovered.begin(),discovered.end(),[&](const Door& d){return d.handle==old.handle;}))discovered.push_back(std::move(old));
 }
 for(auto& d:discovered)if(std::none_of(doors.begin(),doors.end(),[&](const Door& old){return d.handle==old.handle && d.root==old.root;})){
  if(auto ref=d.handle.get())logFile<<"track ref="<<std::hex<<ref->GetFormID()<<" base="<<ref->GetBaseObject()->GetFormID()<<std::dec<<" leaves="<<d.leaves.size()<<" position="<<ref->GetPositionX()<<","<<ref->GetPositionY()<<","<<ref->GetPositionZ()<<'\n';
 }
 doors=std::move(discovered);
 phase.store("idle after update");
}
struct NPCDoorEvents final:RE::BSTEventSink<RE::TESActivateEvent>{
 RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* e,RE::BSTEventSource<RE::TESActivateEvent>*) override{
  if(!e || !e->objectActivated || !e->actionRef || !e->actionRef->As<RE::Actor>())return RE::BSEventNotifyControl::kContinue;
  if(physicalActivation.load() && e->actionRef.get()==RE::PlayerCharacter::GetSingleton())return RE::BSEventNotifyControl::kContinue;
  std::lock_guard lock(stateMutex);for(auto& d:doors)if(d.handle==e->objectActivated->GetHandle()){d.npcTime=5.f;d.nativeLatchTime=5.f;for(auto& l:d.leaves)l.latch.latched=false;break;}
  return RE::BSEventNotifyControl::kContinue;
 }
};
static NPCDoorEvents npcDoorEvents;
static void suppressDoorHighlight(){
 if(!settings.disableHighlight || !gameReady.load() || menuSuspended.load())return;
 auto process=RE::ProcessLists::GetSingleton();if(!process)return;
 process->ForEachShaderEffect([](RE::ShaderReferenceEffect* effect){
  if(!effect || effect->finished)return RE::BSContainer::ForEachResult::kContinue;
  auto target=effect->target.get();auto base=target?target->GetBaseObject():nullptr;
  const bool higgs=(higgsSelection[0]&&effect->effectData==higgsSelection[0])||(higgsSelection[1]&&effect->effectData==higgsSelection[1]);
  if(DoorSuppressHighlight(settings.disableHighlight,higgs,base&&supported.contains(base->GetFormID())))effect->finished=true;
  return RE::BSContainer::ForEachResult::kContinue;
 });
}
#include "builtin_door_hands.inl"
static void install(){
 auto api=HiggsAPI::GetHiggsInterface();auto data=RE::TESDataHandler::GetSingleton(false);
 if(!api || !data){logFile<<"HIGGS or data handler unavailable; no models changed\n";logFile.flush();return;}
 loadSettings();
 activatorsOwnRiftenStall=GetModuleHandleW(L"InteractiveActivatorsVR.dll") &&
  (data->LookupLoadedModByName("InteractiveActivatorsVR.esp") ||
   data->LookupLoadedLightModByName("InteractiveActivatorsVR.esp"));
 if(activatorsOwnRiftenStall)logFile<<"Riften market stall door delegated to Interactive Activators VR\n";
 higgsSelection[0]=data->LookupForm<RE::TESEffectShader>(0x6F00,"higgs_vr.esp");higgsSelection[1]=data->LookupForm<RE::TESEffectShader>(0x6F01,"higgs_vr.esp");
 logFile<<"HIGGS API build="<<api->GetBuildNumber()<<"; discovery identity=form-id profile cache\n";
 capsuleMassThreshold=configuredCapsuleMass(api);
 logFile<<"player capsule threshold="<<capsuleMassThreshold<<"; scalar mass floor on ordinary AND teleport doors; rotational inertia preserved\n";logFile.flush();
 for(auto door:data->GetFormArray<RE::TESObjectDOOR>()){
  if(!door)continue;std::string name=NormalizeDoorModel(door->GetModel()?door->GetModel():"");
  for(const auto& model:MODELS)if(DoorModelMatches(name,model.original,model.physical)){
   if(delegatedDoor(model))break;
   auto path=std::filesystem::path("Data/meshes")/model.physical;
   if(!std::filesystem::exists(path)){logFile<<"missing mesh "<<model.physical<<'\n';break;}
   if(name==model.original)door->SetModel(model.physical);
   supported.insert(door->GetFormID());assignedProfiles.emplace(door->GetFormID(),&model);break;
  }
 }
 if(auto events=RE::ScriptEventSourceHolder::GetSingleton())events->AddEventSink<RE::TESActivateEvent>(&npcDoorEvents);
 if(auto ui=RE::UI::GetSingleton())ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuEvents);
 if(auto module=GetModuleHandleW(L"PhysicalCollisionVR.dll")){
  pcvrPresent=true;
  pcvrBareHandGetter=reinterpret_cast<RE::hkpRigidBody*(*)(bool)>(GetProcAddress(module,"PCVR_GetBareHandRigidBodyV1"));
  pcvrHandRayGetter=reinterpret_cast<std::uint32_t(*)()>(GetProcAddress(module,"PCVR_GetDoorHandRayGroupV1"));
  auto base=reinterpret_cast<const std::byte*>(module);auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  static constexpr unsigned char signature[]={0xba,0x1e,0x0,0x0,0x0,0x44,0x88,0x65,0xf0,0x48,0x8d,0x4d,0xf4,0xe8,0x7b,0x6b,0x7,0x0,0x8b,0x15,0xd9,0x7e,0x1f,0x0,0x48,0x8d,0x4d,0xf4,0xe8,0xcc,0x6b,0x7,0x0,0x49,0x8b,0x7,0x48,0x8d,0x55,0xd0,0x49,0x8b,0xcf,0xff,0x90,0x98,0x1,0x0,0x0};
  if(dos->e_magic==IMAGE_DOS_SIGNATURE){auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
   if(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp==0x6abe160f && nt->OptionalHeader.SizeOfImage>0x2a5838 &&
      std::memcmp(base+0xad943,signature,sizeof(signature))==0){
    pcvrHandRayGroup=reinterpret_cast<volatile const std::uint32_t*>(base+0x2a5834);
   }
  }
 }
 builtinDoorHands=SelectDoorHandMode(pcvrPresent,pcvrBareHandGetter!=nullptr,PcvrAPI::g_pcvr2!=nullptr)==2;
 logFile<<"door hand mode="<<(!pcvrPresent?"HIGGS grab only":builtinDoorHands?"built-in, public PCVR API2+":pcvrBareHandGetter?"existing compatible bridge":"unsupported PCVR API; HIGGS grab only")<<'\n';
 logFile<<"PCVR hand-ray bridge="<<(pcvrHandRayGroup!=nullptr || pcvrHandRayGetter!=nullptr)<<"; stack walking disabled\n";
 api->AddPreVrikPreHiggsCallback(restoreBuiltinHands);api->AddPostVrikPreHiggsCallback(sampleBuiltinHands);
 api->AddPostVrikPreHiggsCallback(repairSelectionLinks);api->AddCollisionFilterComparisonCallback(comparePortal);api->AddPrePhysicsStepCallback(physics);api->AddPostVrikPostHiggsCallback(update);api->AddPostVrikPostHiggsCallback(enforceAnchors);api->AddPostVrikPostHiggsCallback(applyBuiltinHands);api->AddGrabbedCallback(grabbed);api->AddDroppedCallback(dropped);api->AddPostVrikPostHiggsCallback(suppressDoorHighlight);
 logFile<<"G1.41 Riften stall delegation; validated lockpick intent; gravity lids; NPC contact native-open fallback; capsule-only actor contacts; permeable grilles; confirmed released close-state sync; physical display-case hinges; reference lifecycle repair; native display-case fallback; portal threshold=12deg; integrated optional hand interaction; half-second sustained palm contact gate; native ordinary-door state sync; release grace=0.5s; registered; patched base doors="<<supported.size()<<" models="<<std::size(MODELS)<<"; contour colliders; per-leaf wall contact suppression; actor/hand contacts retained; size-based mass; cell tracking reset\n";logFile.flush();
 phase.store("waiting for save load / native model loading");
}
SKSE_EXPORT constinit SKSE::PluginDeclaration SKSEPlugin_Version({
 .Version=REL::Version{0,1,41,0},.Name="SVRPhysicalDoors"sv,.Author="Local physical doors"sv,
 .StructCompatibility=SKSE::StructCompatibility::Dependent,
 .RuntimeCompatibility={REL::Version{1,4,15,0},REL::Version{1,4,15,1}},.MinimumSKSEVersion=REL::Version{0,0,0,0}
});
SKSE_PLUGIN_QUERY(const SKSE::QueryInterface* skse,SKSE::PluginInfo* info){
 info->infoVersion=SKSE::PluginInfo::kVersion;info->name="SVRPhysicalDoors";info->version=1;
 return !skse->IsEditor()&&(skse->RuntimeVersion()==REL::Version{1,4,15,0}||skse->RuntimeVersion()==REL::Version{1,4,15,1});
}
SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse){
 SKSE::Init(skse);if(auto p=SKSE::log::log_directory()){
  logFile.open(*p/"SVRPhysicalDoors.log");
  crashFile=CreateFileW((*p/"SVRPhysicalDoors-crash.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
  AddVectoredExceptionHandler(0,recordFault);
 }
 SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* m){
  PcvrAPI::AcceptInterfaceOffer(m);
  if(m->type==SKSE::MessagingInterface::kDataLoaded)install();
  else if(m->type==SKSE::MessagingInterface::kPreLoadGame || m->type==SKSE::MessagingInterface::kNewGame){
   gameReady.store(false);
   std::lock_guard lock(stateMutex);restoreHidden();doors.clear();queuedActivation.clear();leafPickFilters.store(std::make_shared<const std::unordered_set<std::uint32_t>>());menuSuspended.store(false);
   if(m->type==SKSE::MessagingInterface::kNewGame)gameReady.store(true);
  }else if(m->type==SKSE::MessagingInterface::kPostLoadGame){
   // SKSE dispatches the boolean as (void*)0 or (void*)1, not a pointer to a bool.
   bool success=m->data!=nullptr;
   gameReady.store(success);logFile<<"post load success="<<success<<'\n';logFile.flush();
  }
 });return true;
}
