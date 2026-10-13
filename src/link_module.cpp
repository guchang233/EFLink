// EF Link: LAN multiplayer for Arknights: Endfield as a Better-Endfield third-party module.
// The network runs on its own thread; drawing remote players runs on the game thread through
// CameraManager.TailLateTick; the module page and other modules control it through messages
// and the exported EFLink_GetApiV1 table.
#include "link_world.h"
#include "link_damage_scope.h"
#include "BetterEndfield/ThirdPartyModule.h"
#include "EndfieldLink/EndfieldLink.h"
#include "runtime_scope.h"
#include "nlohmann/json.hpp"
#include "link_gui.h"
#include <type_traits>
#include <map>
#include <deque>
#include "link_settings.h"
#include "link_version.h"
#include "authority/native_proto_codec.h"

using nlohmann::json;
namespace {
#ifndef EFLINK_MODULE_ID
#define EFLINK_MODULE_ID "eflink.core"
#endif
constexpr char kId[]=EFLINK_MODULE_ID;
constexpr char kVersion[]=EFLINK_VERSION;

BE_ThirdPartyHostV1 host{};
std::recursive_mutex lifecycle;
bool initialized=false;
eflink::Net net;
eflink::World world;
eflink::Session session;
eflink::Gui gui;
std::recursive_mutex worldMutex;
std::atomic<bool> hooked=false,canvasHooked=false,cleanupRequested=false,cleanupDone=false;
std::jthread hookWorker;
json configuration=json::object();
std::string hookError;
std::string guiError;

// ---- channels and listeners for other modules ----
struct Channel {std::string name;std::uint32_t hash;EFLink_ReceiveFn receive;void* user;};
struct Listener {EFLink_PeerFn callback;void* user;};
// Unregistering waits for in-flight callbacks. Recursive to allow self-removal.
std::recursive_mutex registryMutex;
std::map<std::uint32_t,Channel> channels;
std::map<std::uint32_t,Listener> listeners;
std::uint32_t nextHandle=1;

void deliver(std::uint16_t from,std::uint32_t hash,const unsigned char* data,std::size_t size) {
    if(hash==eflink::worldChannel) {session.receive(from,data,size);return;}
    std::lock_guard lock(registryMutex);
    std::vector<std::uint32_t> handles;for(const auto& [handle,c]:channels) if(c.hash==hash) handles.push_back(handle);
    for(auto handle:handles) if(auto it=channels.find(handle);it!=channels.end()) {auto c=it->second;if(c.receive) {try {c.receive(c.user,from,data,std::uint32_t(size));} catch(...) {}}}
}
void peerChanged(std::uint16_t peer,bool joined) {
    std::lock_guard lock(registryMutex);
    std::vector<std::uint32_t> handles;for(const auto& [handle,l]:listeners) { (void)l;handles.push_back(handle);}
    for(auto handle:handles) if(auto it=listeners.find(handle);it!=listeners.end()) {auto l=it->second;if(l.callback) {try {l.callback(l.user,peer,joined?1:0);} catch(...) {}}}
}

// ---- game thread ----
using Tick=void(__fastcall*)(void*,float,void*);
Tick nextTick=nullptr;
BE_ResolvedMethodV1 tickMethod{};
const void* cameraManagerClass=nullptr;
const BE_HostApiV1* runtime() {return host.get_runtime?host.get_runtime(host.context):host.runtime;}
using ClassOf=const void*(*)(void*);using ParentOf=const void*(*)(const void*);
ClassOf classOf=nullptr;ParentOf parentOf=nullptr;
bool isCameraManager(void* instance) {
    if(!classOf||!parentOf) return false;
    __try {
        for(auto* klass=classOf(instance);klass;klass=parentOf(klass)) if(klass==cameraManagerClass) return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    return false;
}
void __fastcall onTick(void* instance,float delta,void* info) {
    if(nextTick) nextTick(instance,delta,info);
    // Identical native bodies can be folded together; only CameraManager's tick counts.
    if(!instance||(info&&info!=tickMethod.method_info)||!isCameraManager(instance)) return;
    std::lock_guard lock(worldMutex);
    auto* api=runtime();if(!api) return;
    if(cleanupRequested.load()) {
        if(!canvasHooked.load()&&!cleanupDone.load()) {try {world.shutdown();cleanupDone.store(true);} catch(...) {}}
        return;
    }
    world.tick(api,net,session);
}
using CanvasTick=void(__fastcall*)(void*);
CanvasTick nextCanvas=nullptr;BE_ResolvedMethodV1 canvasMethod{};
void __fastcall onCanvas(void* info) {
    if(nextCanvas) nextCanvas(info);
    if(info&&info!=canvasMethod.method_info) return;
    static thread_local bool rendering=false;if(rendering) return;
    // Consistent lock order: GUI actions can re-enter lifecycle and worldMutex.
    std::unique_lock lock(lifecycle,std::try_to_lock);if(!lock.owns_lock()) return;
    std::lock_guard lockWorld(worldMutex);auto* api=runtime();if(!api) return;
    rendering=true;
    try {
        if(cleanupRequested.load()) {
            if(!cleanupDone.load()) {gui.shutdownGame(api);world.shutdown();cleanupDone=true;}
        } else if(initialized) {if(world.takeHostMapNotice())gui.open();gui.tickGame(api);world.menu(api,gui.visible());}
    } catch(...) {}
    rendering=false;
}
using ApplyDamage=int(__fastcall*)(void*,void*,void*);
ApplyDamage nextDamage=nullptr;BE_ResolvedMethodV1 damageMethod{};bool damageHooked=false;
using EntityDie=void(__fastcall*)(void*,void*);
using FindEnemyTarget=void*(__fastcall*)(void*,int*,void*);
FindEnemyTarget nextEnemyTarget=nullptr;BE_ResolvedMethodV1 enemyTargetMethod{};bool enemyTargetHooked=false;
using AssignEnemyTarget=void(__fastcall*)(void*,void*,void*);
AssignEnemyTarget nextAssignEnemyTarget=nullptr;BE_ResolvedMethodV1 assignEnemyTargetMethod{};bool assignEnemyTargetHooked=false;
void __fastcall onAssignEnemyTarget(void* controller,void* target,void* info) {
    if(!nextAssignEnemyTarget)return;
    if(info&&info!=assignEnemyTargetMethod.method_info){nextAssignEnemyTarget(controller,target,info);return;}
    std::lock_guard lock(worldMutex);
    if(!cleanupRequested)world.roomEnemyAssignTarget(net,controller,target);
    nextAssignEnemyTarget(controller,target,info);
}
using EnableEnemyFind=bool(__fastcall*)(void*,void*);
EnableEnemyFind nextEnableEnemyFind=nullptr;BE_ResolvedMethodV1 enableEnemyFindMethod{};bool enableEnemyFindHooked=false;
bool __fastcall onEnableEnemyFind(void* controller,void* info) {
    if(!nextEnableEnemyFind)return false;
    if(info&&info!=enableEnemyFindMethod.method_info)return nextEnableEnemyFind(controller,info);
    std::lock_guard lock(worldMutex);bool enabled=false;
    if(!cleanupRequested&&world.roomEnemyFindEnabled(net,controller,enabled))return enabled;
    return nextEnableEnemyFind(controller,info);
}
void* __fastcall onEnemyTarget(void* controller,int* range,void* info) {
    if(!nextEnemyTarget)return nullptr;
    if(!range||(info&&info!=enemyTargetMethod.method_info))return nextEnemyTarget(controller,range,info);
    std::lock_guard lock(worldMutex);void* selected=nullptr;int nativeRange=0;
    if(!cleanupRequested&&world.selectRoomEnemyTarget(net,controller,selected,nativeRange)) {*range=nativeRange;return selected;}
    return nextEnemyTarget(controller,range,info);
}
EntityDie nextDie=nullptr;BE_ResolvedMethodV1 dieMethod{};bool dieHooked=false;
void __fastcall onDie(void* entity,void* info) {
    if(!nextDie)return;
    if(info&&info!=dieMethod.method_info) {nextDie(entity,info);return;}
    std::lock_guard lock(worldMutex);
    if(!cleanupRequested&&world.guestBlocksDeath(net,entity))return;
    auto observation=cleanupRequested?std::optional<eflink::World::DeathObservation>{}:world.beforeNativeDeath(net,entity);
    nextDie(entity,info);
    if(observation)world.afterNativeDeath(*observation);
}
// Windows x64: TargetHandleView is exactly one pointer-sized value; the
// Nullable<CastSkillOptions> value (> 8 bytes) is passed indirectly. Validate
// the target view layout from runtime metadata before installing this hook.
using CastSkill=bool(__fastcall*)(void*,void*,void*,void*,void*,void*,void*,void*,bool,void*);
CastSkill nextCast=nullptr;BE_ResolvedMethodV1 castMethod{};bool castHooked=false;
using SkillEnd=void(__fastcall*)(void*,int,int,void*,void*);
SkillEnd nextSkillEnd=nullptr;BE_ResolvedMethodV1 skillEndMethod{};bool skillEndHooked=false;
void __fastcall onSkillEnd(void* skill,int finish,int reason,void* context,void* info) {
    if(!nextSkillEnd)return;
    if(info&&info!=skillEndMethod.method_info) {nextSkillEnd(skill,finish,reason,context,info);return;}
    std::lock_guard lock(worldMutex);
    auto observed=cleanupRequested?std::optional<eflink::World::PlayerCastEndObservation>{}:world.beforePlayerCastEnd(skill,finish);
    nextSkillEnd(skill,finish,reason,context,info);
    if(observed&&!cleanupRequested)world.afterPlayerCastEnd(net,*observed,finish);
}
using GetCharInfo=void*(__fastcall*)(void*,std::uint64_t,void*);
using TryGetCharInfo=bool(__fastcall*)(void*,std::uint64_t,void**,bool,void*);
GetCharInfo nextCharInfo=nullptr;TryGetCharInfo nextTryCharInfo=nullptr;
BE_ResolvedMethodV1 charInfoMethod{},tryCharInfoMethod{};bool charInfoHooked=false,tryCharInfoHooked=false;
using WeaponModelPath=bool(__fastcall*)(int,std::uint64_t,void**,void*);
WeaponModelPath nextWeaponPath=nullptr;BE_ResolvedMethodV1 weaponPathMethod{};bool weaponPathHooked=false;
bool __fastcall onWeaponPath(int scope,std::uint64_t id,void** output,void* info) {
    if(!nextWeaponPath)return false;
    if(output&&(!info||info==weaponPathMethod.method_info)) {
        std::lock_guard lock(worldMutex);bool handled=false;
        if(!cleanupRequested)try {
            const bool result=world.roomWeaponModelPath(id,output,handled);if(handled)return result;
        }catch(...) {if(handled){*output=nullptr;return false;}}
    }
    return nextWeaponPath(scope,id,output,info);
}
void* __fastcall onCharInfo(void* bag,std::uint64_t id,void* info) {
    if(!nextCharInfo)return nullptr;
    if(!info||info==charInfoMethod.method_info) {
        std::lock_guard lock(worldMutex);
        if(!cleanupRequested)try {if(auto* room=world.roomCharacterInfo(id))return room;}catch(...) {}
    }
    return nextCharInfo(bag,id,info);
}
bool __fastcall onTryCharInfo(void* bag,std::uint64_t id,void** output,bool quiet,void* info) {
    if(!nextTryCharInfo)return false;
    if(output&&(!info||info==tryCharInfoMethod.method_info)) {
        std::lock_guard lock(worldMutex);
        if(!cleanupRequested)try {if(auto* room=world.roomCharacterInfo(id)) {*output=room;return true;}}catch(...) {}
    }
    return nextTryCharInfo(bag,id,output,quiet,info);
}
bool __fastcall onCast(void* ability,void* skill,void* target,void* ended,void* options,void* exclusive,void* started,void* castInfo,bool skip,void* info) {
    if(!nextCast)return false;
    if(info&&info!=castMethod.method_info)return nextCast(ability,skill,target,ended,options,exclusive,started,castInfo,skip,info);
    // Never consumes a cast: players keep their own native combat. The host only
    // observes room enemies so guests can replay the same attack on their copy.
    std::lock_guard lock(worldMutex);
    const bool accepted=nextCast(ability,skill,target,ended,options,exclusive,started,castInfo,skip,info);
    if(accepted&&!cleanupRequested) {
        if(skillEndHooked)world.observePlayerCastBegin(net,ability,skill,target);
        world.observeEnemyCast(net,ability,skill,target);
    }
    return accepted;
}
int __fastcall onDamage(void* ability,void* modifier,void* info) {
    if(!nextDamage) return 0;
    if(info&&info!=damageMethod.method_info) return nextDamage(ability,modifier,info);
    std::lock_guard lock(worldMutex);
    if(!cleanupRequested)world.damageHookSeen();
    if(!cleanupRequested&&world.blocksLocalDamage(ability))return 0;
    eflink::DamageApplicationScope scope(ability,modifier);
    if(!scope.observe())return nextDamage(ability,modifier,info);
    if(!cleanupRequested&&world.captureDamage(net,ability,modifier)) return 0;
    auto observation=cleanupRequested?std::optional<eflink::World::DamageObservation>{}:world.beforeNativeDamage(net,ability,modifier);
    const auto result=nextDamage(ability,modifier,info);
    if(observation)world.afterNativeDamage(net,*observation,result);
    if(!cleanupRequested)world.noteDamage("apply",ability,std::to_string(result));
    return result;
}
// Damage a body part passes on to its owner (monsters created with official
// part data). Guest hits on room monsters are settled by the host here too.
using ApplyTransferred=int(__fastcall*)(void*,void*,bool,void*);
ApplyTransferred nextTransferred=nullptr;BE_ResolvedMethodV1 transferredMethod{};bool transferredHooked=false;
int __fastcall onTransferred(void* ability,void* modifier,bool triggerEvent,void* info) {
    if(!nextTransferred) return 0;
    if(info&&info!=transferredMethod.method_info) return nextTransferred(ability,modifier,triggerEvent,info);
    std::lock_guard lock(worldMutex);
    if(!cleanupRequested&&world.blocksLocalDamage(ability))return 0;
    eflink::DamageApplicationScope scope(ability,modifier);
    if(!scope.observe())return nextTransferred(ability,modifier,triggerEvent,info);
    if(!cleanupRequested&&world.captureDamage(net,ability,modifier))return 0;
    auto observation=cleanupRequested?std::optional<eflink::World::DamageObservation>{}:world.beforeNativeDamage(net,ability,modifier);
    const auto result=nextTransferred(ability,modifier,triggerEvent,info);
    if(observation)world.afterNativeDamage(net,*observation,result);
    if(!cleanupRequested)world.noteDamage("transferred",ability,std::to_string(result));
    return result;
}
// Observation only: whether the client considers the target immune.
using DamageImmune=bool(__fastcall*)(void*,void*,void*);
DamageImmune nextImmune=nullptr;BE_ResolvedMethodV1 immuneMethod{};bool immuneHooked=false;
bool __fastcall onImmune(void* ability,void* modifier,void* info) {
    if(!nextImmune) return false;
    const bool immune=nextImmune(ability,modifier,info);
    if(!info||info==immuneMethod.method_info) {
        std::lock_guard lock(worldMutex);
        if(!cleanupRequested)world.noteDamage("immune_check",ability,immune?"immune":"open");
    }
    return immune;
}
// Exact metadata signatures, shared hook ownership, and a role-dependent gate.
// No input interception or sticky gameplay/UI-forbid handles are involved.
template<int Slot,class Result,bool BlockValue,class... Args> struct GuestGate {
    using Next=Result(__fastcall*)(void*,Args...,void*);
    static inline Next next=nullptr;
    static inline BE_ResolvedMethodV1 method{};
    static inline const char* label="";
    static Result __fastcall call(void* self,Args... args,void* info) {
        if(!next) {if constexpr(!std::is_void_v<Result>)return Result(BlockValue);else return;}
        if(!info||info==method.method_info) {
            std::lock_guard lock(worldMutex);
            if(!cleanupRequested&&world.blockPrivateAction(net,label)) {
                if constexpr(!std::is_void_v<Result>)return Result(BlockValue);else return;
            }
        }
        return next(self,args...,info);
    }
    static bool install(const BE_HostApiV1* api,const char* space,const char* type,const char* name,const char* params,const char* result) {
        label=name;unsigned count=params[0]?1:0;for(auto* p=params;*p;++p)if(*p=='|')++count;
        BE_MethodDescriptorV1 descriptor{"Gameplay.Beyond.dll",space,type,name,params[0]?params:nullptr,result,count};
        if(api->resolve_method(api->context,&descriptor,&method)!=BE_Result_Ok||!method.method_pointer)return false;
        std::uint64_t handle=0;
        return host.hooks->create(host.hooks->context,kId,method.method_pointer,reinterpret_cast<void*>(call),reinterpret_cast<void**>(&next),&handle)==BE_Result_Ok;
    }
};
bool installGuestGates(const BE_HostApiV1* api) {
    bool ready=true;std::string error;
    auto require=[&](bool ok,const char* name){if(!ok){ready=false;error+=std::string(name)+"; ";}};
    require(GuestGate<0,bool,true>::install(api,"Beyond.Gameplay","InteractOptionManagerForInteractive","_GlobalBlocked","","System.Boolean"),"interact_options");
    require(GuestGate<1,bool,false>::install(api,"Beyond.Gameplay.Core","InteractiveBaseTrigger","GetConditionResult","","System.Boolean"),"interact_condition");
    require(GuestGate<2,void,false>::install(api,"Beyond.Gameplay.Core","InteractiveBaseTrigger","_EvaluateConditionFinished","","System.Void"),"interact_execute");
    require(GuestGate<3,void,false>::install(api,"Beyond.Gameplay.Core","StepOnTrigger","OnEvaluateConditionSucceed","","System.Void"),"step_trigger");
    require(GuestGate<4,void,false>::install(api,"Beyond.Gameplay.Core","ScanTrigger","OnEvaluateConditionSucceed","","System.Void"),"scan_trigger");
    require(GuestGate<5,bool,false>::install(api,"Beyond.Gameplay.Core","ScanTrigger","CanScan","","System.Boolean"),"scan_query");
    require(GuestGate<6,void,false,void*>::install(api,"Beyond.Gameplay.Core","AttackTrigger","_AttackEventFilter","Beyond.Gameplay.Core.EventParams","System.Void"),"attack_interactive");
    require(GuestGate<7,void,false,bool,bool>::install(api,"Beyond.Gameplay","TriggerVolumeManager","CheckTriggers","System.Boolean|System.Boolean","System.Void"),"level_trigger_volumes");
    require(GuestGate<8,bool,false,void*,void*>::install(api,"Beyond.Gameplay.Core","DungeonManager","CheckCanEnterDungeonAndToast","System.String|System.String","System.Boolean"),"dungeon_check");
    require(GuestGate<9,bool,false,void*,void*,bool>::install(api,"Beyond.Gameplay.Core","DungeonManager","TryReqEnterDungeon","Proto.CS_ENTER_DUNGEON|System.String|System.Boolean","System.Boolean"),"dungeon_request_packet");
    require(GuestGate<10,bool,false,void*,void*,int>::install(api,"Beyond.Gameplay.Core","DungeonManager","TryReqEnterDungeon","System.String|System.String|System.Int32","System.Boolean"),"dungeon_request");
    require(GuestGate<11,bool,false,void*,void*,void*,int>::install(api,"Beyond.Gameplay.Core","DungeonManager","TryReqEnterDungeon","System.String|System.Collections.Generic.List<Beyond.Gameplay.CharInfo>|System.String|System.Int32","System.Boolean"),"dungeon_request_squad");
    require(GuestGate<12,void,false,void*>::install(api,"Beyond.Gameplay.Core","DungeonManager","TouchDungeonEntrance","System.String","System.Void"),"dungeon_entrance");
    require(GuestGate<13,void,false,void*,eflink::unity::V3,bool>::install(api,"Beyond.Gameplay","GameplayNetwork","SendKillTree","System.String|UnityEngine.Vector3|System.Boolean","System.Void"),"resource_tree");
    require(GuestGate<14,void,false,void*,void*>::install(api,"Beyond.Gameplay","GameplayNetwork","C2STeleportWithValidationData","Beyond.Gameplay.TeleportValidationData|System.Action","System.Void"),"account_teleport_validation");
    require(GuestGate<15,void,false,int,eflink::unity::V3,eflink::unity::V3,int,int,int,void*,std::uint32_t>::install(api,"Beyond.Gameplay","GameplayNetwork","C2STeleport","System.Int32|UnityEngine.Vector3|UnityEngine.Vector3|Beyond.GEnums.C2STeleportReason|Beyond.Gameplay.TeleportUIType|Beyond.Gameplay.TeleportOptions|System.Action|System.UInt32","System.Void"),"account_teleport");
    require(GuestGate<16,void,false,void*,std::uint64_t,bool>::install(api,"Beyond.Gameplay","GameMechanicsSystem","SendReqStartGameMechanic","System.String|System.UInt64|System.Boolean","System.Void"),"subgame_request");
    require(GuestGate<17,void,false,void*,bool>::install(api,"Beyond.Gameplay","GameMechanicsSystem","SendReqStartGameMsg","Proto.CS_GAME_MECHANICS_REQ_START|System.Boolean","System.Void"),"subgame_request_packet");
    require(GuestGate<18,void,false,std::uint64_t,std::uint64_t>::install(api,"Beyond.Gameplay.Core.DynamicScene","DynamicSceneTriggerSystem","OnDynamicTriggerEnter","System.UInt64|System.UInt64","System.Void"),"dynamic_scene_trigger");
    require(GuestGate<19,void,false,void*,void*,eflink::unity::V3,eflink::unity::V3>::install(api,"Beyond.Gameplay","GameplayNetwork","C2STeleportWithValidationData","Beyond.Gameplay.TeleportValidationData|System.Action|UnityEngine.Vector3|UnityEngine.Vector3","System.Void"),"account_teleport_validation_position");
    {std::lock_guard lock(worldMutex);world.setGuestGates(ready,std::move(error));}
    return ready;
}
using MarkTargetScore=bool(__fastcall*)(void*,void*,void*,float*,bool*,void*);
MarkTargetScore nextMarkTargetScore=nullptr;BE_ResolvedMethodV1 markTargetScoreMethod{};
bool __fastcall onMarkTargetScore(void* manager,void* ability,void* priority,float* score,bool* onScreen,void* info) {
    if(!nextMarkTargetScore)return false;
    if(!info||info==markTargetScoreMethod.method_info) {
        std::lock_guard lock(worldMutex);
        try {if(!cleanupRequested&&!world.allowRoomTarget(net,ability))return false;}
        catch(...) {return false;} // An unloaded candidate cannot be selected.
    }
    return nextMarkTargetScore(manager,ability,priority,score,onScreen,info);
}
// Guest in a native room: this account's monster enter/leave messages are
// withheld (and replayed on leave); everything else reaches the game unchanged.
using ReceiveMessage=void(__fastcall*)(void*,const void*,void*);
ReceiveMessage nextReceive=nullptr;BE_ResolvedMethodV1 receiveMethod{};
std::size_t responseBodyOffset=0;eflink::authority::NativeProtoCodec officialCodec;bool officialFilterReady=false;
eflink::authority::NativeFeed officialViewParser;
void __fastcall onReceive(void* bus,const void* response,void* info) {
    alignas(8) unsigned char filtered[32]{};bool replaced=false;
    std::unique_ptr<eflink::unity::Api::TemporaryRoot> replacementRoot;
    if(nextReceive&&officialFilterReady&&response&&(!info||info==receiveMethod.method_info))try {
        void* body=nullptr;std::memcpy(&body,static_cast<const unsigned char*>(response)+responseBodyOffset,sizeof body);
        if(body) {
            auto dll=GetModuleHandleW(L"GameAssembly.dll");
            auto objectClass=reinterpret_cast<const void*(*)(void*)>(GetProcAddress(dll,"il2cpp_object_get_class"));
            auto nameOf=reinterpret_cast<const char*(*)(const void*)>(GetProcAddress(dll,"il2cpp_class_get_name"));
            const char* name=objectClass&&nameOf?nameOf(objectClass(body)):nullptr;
            // Observe official equipment metadata without changing or consuming
            // its response. Account/weapon instance IDs remain in a local cache.
            if(name&&(std::strcmp(name,"SC_LOGIN")==0||eflink::authority::OfficialAppearance::accepts(name))) {
                std::lock_guard lock(worldMutex);
                if(!cleanupRequested) {
                    if(std::strcmp(name,"SC_LOGIN")==0)world.resetAppearance();
                    else try {world.observeAppearance(name,eflink::authority::NativeFeed::decode64(officialCodec.serialize(body)));}
                    catch(...) {world.rejectAppearance();}
                }
            }
            if(name&&(std::strcmp(name,"SC_OBJECT_ENTER_VIEW")==0||std::strcmp(name,"SC_OBJECT_LEAVE_VIEW")==0)) {
                std::lock_guard lock(worldMutex);
                if(!cleanupRequested) {
                    const auto bytes=eflink::authority::NativeFeed::decode64(officialCodec.serialize(body));
                    world.observeOfficial(name,bytes);
                    const auto split=world.filtering()?eflink::authority::splitOfficialView(name,bytes):eflink::authority::OfficialViewSplit{};
                    if(split.changed) {
                        bool hasReplacement=false;
                        if(!split.visible.empty()) {
                            // Keep headMsg, packing state and receive timestamp.
                            // Only msgBody changes; never consume official seq IDs.
                            auto* replacement=officialViewParser.parseView(name,split.visible);
                            replacementRoot=std::make_unique<eflink::unity::Api::TemporaryRoot>(runtime(),replacement);
                            std::memcpy(filtered,response,sizeof filtered);
                            std::memcpy(filtered+responseBodyOffset,&replacement,sizeof replacement);
                            hasReplacement=true;
                        }
                        // Commit only after the replacement parsed successfully.
                        if(world.withholdOfficial(name,split.withheld)) {
                            replaced=hasReplacement;
                            if(!replaced)return;
                        }
                    }
                }
            }
        }
    }catch(...) {}
    // Dispatch once, outside the preparation catch. A native handler failure
    // must never cause a second dispatch of the unsplit private-world message.
    if(nextReceive)nextReceive(bus,replaced?filtered:response,info);
}
// ServerReporter.CreateServerOp<T>(opType, data, ref OwnerInfo{serverId,...}) is
// generic, so its shared body has no metadata pointer. It is the one target
// the three non-generic op creators each call exactly once and that the
// reporter's flush/upload code never calls.
// Outbound census state (roadmap M1 observation).
std::mutex outboundMutex;std::map<std::string,std::uint64_t> outboundCounts[3]; // outside, guest in host world, hosting
std::deque<std::string> outboundBattleOps; // guest in host world: serialized CS_BATTLE_OP, bounded
std::map<std::pair<std::uint64_t,std::uint64_t>,std::uint64_t> createdOps; // guest in host world: allowed ops by (source id, battle instance)
using CreateServerOp=void(__fastcall*)(void*,int,void*,const std::uint64_t*,void*);
CreateServerOp nextCreateOp=nullptr;bool opFilterReady=false;std::string opFilterError;
void __fastcall onCreateServerOp(void* reporter,int type,void* data,const std::uint64_t* owner,void* info) {
    if(owner&&opFilterReady) {
        std::lock_guard lock(worldMutex);
        // Pure provenance checks stay active during native shutdown too; that
        // shutdown can synchronously create the same withdrawal operations.
        if(world.suppressServerOp(owner[0],owner[1]))return;
        if(world.outboundScope()==1) {
            std::lock_guard census(outboundMutex);const std::pair key{owner[0],owner[1]};
            if(createdOps.size()<1024||createdOps.contains(key))++createdOps[key];
        }
    }
    if(nextCreateOp)nextCreateOp(reporter,type,data,owner,info);
}
std::map<std::uintptr_t,int> directCalls(const void* code) {
    std::map<std::uintptr_t,int> calls;if(!code)return calls;
    auto* p=static_cast<const unsigned char*>(code);
    MEMORY_BASIC_INFORMATION mbi{};if(!VirtualQuery(p,&mbi,sizeof mbi))return calls;
    const auto end=reinterpret_cast<const unsigned char*>(mbi.BaseAddress)+mbi.RegionSize;
    for(std::size_t i=0;i<4096&&p+i+5<=end;++i) {
        if(p[i]==0xCC&&p[i+1]==0xCC)break; // padding after the function
        if(p[i]==0xE8) {std::int32_t rel;std::memcpy(&rel,p+i+1,4);++calls[reinterpret_cast<std::uintptr_t>(p+i+5)+std::intptr_t(rel)];}
    }
    return calls;
}
void installOpFilter(const BE_HostApiV1* api) {
    auto resolve=[&](const char* name,const char* params,const char* result,unsigned count)->const void* {
        BE_MethodDescriptorV1 d{"Gameplay.Beyond.dll","Beyond.Gameplay.Core","BattleManager/ServerReporter",name,params,result,count};BE_ResolvedMethodV1 r{};
        return api->resolve_method(api->context,&d,&r)==BE_Result_Ok?r.method_pointer:nullptr;};
    const void* creators[]={
        resolve("CreateModifyEntityValueOp","Beyond.Gameplay.Core.Entity|System.Double|System.Single|Beyond.Gameplay.Core.BattleManager.ServerReporter.ModifyValueType","Proto.BATTLE_ENTITY_VALUE_MODIFY_DATA",4),
        resolve("CreateServerActionOp","Beyond.Gameplay.Core.AbilityAction.ServerActionParams&","System.Void",1),
        resolve("CreateUpdateAtbInfoOp","Proto.BATTLE_UPDATE_ATB_INFO_REASON|Proto.TEMPLATE_ID|Proto.TEMPLATE_ID|System.Double","System.Void",4)};
    const void* others[]={resolve("Flush",nullptr,"System.Void",0),resolve("TryUpLoad","System.Boolean","System.Void",1)};
    for(auto* c:creators)if(!c){opFilterError="op creators unresolved";return;}
    auto first=directCalls(creators[0]);std::vector<std::uintptr_t> candidates;
    for(const auto& [target,count]:first) {
        if(count!=1)continue;bool everywhere=true;
        for(int i=1;i<3&&everywhere;++i){auto calls=directCalls(creators[i]);everywhere=calls.contains(target)&&calls[target]==1;}
        for(auto* o:others)if(everywhere&&o&&directCalls(o).contains(target))everywhere=false;
        if(everywhere)candidates.push_back(target);
    }
    if(candidates.size()!=1){opFilterError="CreateServerOp not uniquely identified ("+std::to_string(candidates.size())+")";return;}
    std::uint64_t handle=0;
    opFilterReady=host.hooks->create(host.hooks->context,kId,reinterpret_cast<void*>(candidates[0]),reinterpret_cast<void*>(onCreateServerOp),reinterpret_cast<void**>(&nextCreateOp),&handle)==BE_Result_Ok;
    if(!opFilterReady)opFilterError="op filter hook failed";
    else opFilterError="rva "+std::to_string(candidates[0]-reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"GameAssembly.dll")));
}
// Outbound request census (roadmap M1, observation only): counts the protobuf
// type of every HGNetSession.SendMessage call per room role. Every call is
// forwarded unchanged; nothing is stored beyond type names and counts.
using SendMessageFn=std::uint64_t(__fastcall*)(void*,void*,void*);
SendMessageFn nextSend=nullptr;BE_ResolvedMethodV1 sendMethod{};bool outboundObserverReady=false;
std::uint64_t __fastcall onSend(void* netSession,void* message,void* info) {
    if(outboundObserverReady&&message&&(!info||info==sendMethod.method_info))try {
        static const auto dll=GetModuleHandleW(L"GameAssembly.dll");
        static const auto objectClass=reinterpret_cast<const void*(*)(void*)>(GetProcAddress(dll,"il2cpp_object_get_class"));
        static const auto nameOf=reinterpret_cast<const char*(*)(const void*)>(GetProcAddress(dll,"il2cpp_class_get_name"));
        const char* name=objectClass&&nameOf?nameOf(objectClass(message)):nullptr;
        int scope=0;
        {
            std::lock_guard lock(worldMutex);scope=world.outboundScope();
#ifdef EFLINK_NATIVE_DIAGNOSTICS
            // Which client code path sends scene and teleport requests.
            if(name&&(std::strncmp(name,"CS_SCENE_",9)==0||std::strstr(name,"TELEPORT")||std::strstr(name,"REPATRIATE"))&&std::strcmp(name,"CS_SCENE_TOUCH_TRIGGER_VOLUME_REQ")!=0) {
                void* frames[40]{};const auto n=RtlCaptureStackBackTrace(1,40,frames,nullptr);
                std::vector<std::uintptr_t> stack;for(USHORT i=0;i<n;++i)stack.push_back(reinterpret_cast<std::uintptr_t>(frames[i]));
                world.recordSendTrace(name,std::move(stack));
                if(std::strcmp(name,"CS_SCENE_TELEPORT")==0&&world.teleportBodies.size()<8)world.teleportBodies.push_back(officialCodec.serialize(message,64*1024));
            }
#endif
        }
        std::lock_guard lock(outboundMutex);auto& counts=outboundCounts[scope];
        const std::string key=name?name:"?";
        if(counts.size()<512||counts.contains(key))++counts[key];
        if(scope==1&&key=="CS_BATTLE_OP"&&outboundBattleOps.size()<200)
            outboundBattleOps.push_back(officialCodec.serialize(message,256*1024));
    }catch(...) {}
    return nextSend(netSession,message,info);
}
nlohmann::json outboundStatus() {
    std::lock_guard lock(outboundMutex);
    return {{"ready",outboundObserverReady},{"outside",outboundCounts[0]},{"guest_room",outboundCounts[1]},{"hosting",outboundCounts[2]}};
}
void installOutboundObserver(const BE_HostApiV1* api) {
    BE_MethodDescriptorV1 send{"Network.Beyond.dll","Beyond.Network","HGNetSession","SendMessage","Google.Protobuf.IMessage","System.UInt64",1};
    if(api->resolve_method(api->context,&send,&sendMethod)!=BE_Result_Ok||!sendMethod.method_pointer)return;
    std::uint64_t handle=0;
    outboundObserverReady=host.hooks->create(host.hooks->context,kId,sendMethod.method_pointer,reinterpret_cast<void*>(onSend),reinterpret_cast<void**>(&nextSend),&handle)==BE_Result_Ok;
}
void installOfficialFilter(const BE_HostApiV1* api) {
    BE_FieldDescriptorV1 body{"Network.Beyond.dll","Beyond.Network","NetResponse","msgBody","Google.Protobuf.IMessage"};BE_ResolvedFieldV1 field{};
    if(api->resolve_field(api->context,&body,&field)!=BE_Result_Ok||field.offset<16||field.offset>40)return;
    responseBodyOffset=std::size_t(field.offset)-16; // boxed offsets include the object header
    if(responseBodyOffset!=8)return; // verified 32-byte current-client NetResponse layout
    BE_MethodDescriptorV1 rx{"Network.Beyond.dll","Beyond.Network","NetBus","HandleMessage","Beyond.Network.NetResponse","System.Void",1};
    if(api->resolve_method(api->context,&rx,&receiveMethod)!=BE_Result_Ok||!receiveMethod.method_pointer)return;
    try {officialCodec.bind(api);officialViewParser.bind(api);}catch(...) {return;}
    std::uint64_t handle=0;
    officialFilterReady=host.hooks->create(host.hooks->context,kId,receiveMethod.method_pointer,reinterpret_cast<void*>(onReceive),reinterpret_cast<void**>(&nextReceive),&handle)==BE_Result_Ok;
}
BE_Result installHook() {
    if(hooked.load()) return BE_Result_Ok;
    auto* api=runtime();
    if(!api||!api->resolve_method||!api->resolve_class||!host.hooks||!host.hooks->create) return BE_Result_NotReady;
    RuntimeThreadScope scope(true);if(!scope.ready()) return BE_Result_NotReady;
    std::string gameVersion;
    try {gameVersion=eflink::worldauthority::nativeGameVersion(api);}
    catch(const std::exception& e) {hookError=e.what();return BE_Result_NotReady;}
    catch(...) {hookError="native version probe unavailable";return BE_Result_NotReady;}
    if(!eflink::worldauthority::readOnlyBuildSupported(gameVersion)) {
        hookError="未验证游戏版本 "+gameVersion+"；原生游戏适配停用，账号风险不能排除";
        return BE_Result_ContractMismatch;
    }
    if(configuration.value("gui_enabled",true)&&!configuration.value("preview_mode",false)&&!canvasHooked) {
        BE_MethodDescriptorV1 canvas{"UnityEngine.UIModule.dll","UnityEngine","Canvas","SendWillRenderCanvases",nullptr,"System.Void",0};
        if(api->resolve_method(api->context,&canvas,&canvasMethod)!=BE_Result_Ok||!canvasMethod.method_pointer) return BE_Result_NotReady;
        std::uint64_t canvasHandle=0;
        auto result=host.hooks->create(host.hooks->context,kId,canvasMethod.method_pointer,reinterpret_cast<void*>(onCanvas),reinterpret_cast<void**>(&nextCanvas),&canvasHandle);
        if(result!=BE_Result_Ok) return result;canvasHooked=true;
    }
    BE_MethodDescriptorV1 d{"Gameplay.Beyond.dll","Beyond.Gameplay.View","CameraManager","TailLateTick","System.Single","System.Void",1};
    if(api->resolve_method(api->context,&d,&tickMethod)!=BE_Result_Ok||!tickMethod.method_pointer) return BE_Result_NotFound;
    BE_ResolvedClassV1 klass{};
    if(api->resolve_class(api->context,"Gameplay.Beyond.dll","Beyond.Gameplay.View","CameraManager",&klass)!=BE_Result_Ok) return BE_Result_NotFound;
    cameraManagerClass=klass.class_info;
    const auto assembly=GetModuleHandleW(L"GameAssembly.dll");
    classOf=reinterpret_cast<ClassOf>(GetProcAddress(assembly,"il2cpp_object_get_class"));
    parentOf=reinterpret_cast<ParentOf>(GetProcAddress(assembly,"il2cpp_class_get_parent"));
    if(!classOf||!parentOf) return BE_Result_NotReady;
    std::uint64_t handle=0;
    const auto result=host.hooks->create(host.hooks->context,kId,tickMethod.method_pointer,reinterpret_cast<void*>(onTick),reinterpret_cast<void**>(&nextTick),&handle);
    if(result==BE_Result_Ok) {
        hooked.store(true);
        if(!eflink::roomAdapterAvailable)return result;
        installOfficialFilter(api);
        installOpFilter(api);
        installOutboundObserver(api);
        installGuestGates(api);
        BE_MethodDescriptorV1 charInfo{"Gameplay.Beyond.dll","Beyond.Gameplay","CharBagSystem","GetCharInfo","System.UInt64","Beyond.Gameplay.CharInfo",1};
        BE_MethodDescriptorV1 tryCharInfo{"Gameplay.Beyond.dll","Beyond.Gameplay","CharBagSystem","TryGetCharInfo","System.UInt64|Beyond.Gameplay.CharInfo&|System.Boolean","System.Boolean",3};
        if(api->resolve_method(api->context,&charInfo,&charInfoMethod)==BE_Result_Ok&&charInfoMethod.method_pointer) {
            std::uint64_t charHandle=0;charInfoHooked=host.hooks->create(host.hooks->context,kId,charInfoMethod.method_pointer,reinterpret_cast<void*>(onCharInfo),reinterpret_cast<void**>(&nextCharInfo),&charHandle)==BE_Result_Ok;
        }
        if(api->resolve_method(api->context,&tryCharInfo,&tryCharInfoMethod)==BE_Result_Ok&&tryCharInfoMethod.method_pointer) {
            std::uint64_t charHandle=0;tryCharInfoHooked=host.hooks->create(host.hooks->context,kId,tryCharInfoMethod.method_pointer,reinterpret_cast<void*>(onTryCharInfo),reinterpret_cast<void**>(&nextTryCharInfo),&charHandle)==BE_Result_Ok;
        }
        BE_MethodDescriptorV1 weaponPath{"Gameplay.Beyond.dll","Beyond.Gameplay","WeaponUtil","TryGetWeaponModelPathByCharInstId","Beyond.Gameplay.Scope|System.UInt64|System.String&","System.Boolean",3};
        if(api->resolve_method(api->context,&weaponPath,&weaponPathMethod)==BE_Result_Ok&&weaponPathMethod.method_pointer) {
            std::uint64_t pathHandle=0;weaponPathHooked=host.hooks->create(host.hooks->context,kId,weaponPathMethod.method_pointer,reinterpret_cast<void*>(onWeaponPath),reinterpret_cast<void**>(&nextWeaponPath),&pathHandle)==BE_Result_Ok;
        }
        BE_MethodDescriptorV1 markTarget{"Gameplay.Beyond.dll","Beyond.Gameplay.Core","BattleManager","GetMarkTargetScore",
            "Beyond.Gameplay.Core.AbilitySystem|Beyond.Gameplay.Core.BattleManager.MarkTargetPriority&|System.Single&|System.Boolean&","System.Boolean",4};
        if(api->resolve_method(api->context,&markTarget,&markTargetScoreMethod)==BE_Result_Ok&&markTargetScoreMethod.method_pointer) {
            std::uint64_t scoreHandle=0;
            if(host.hooks->create(host.hooks->context,kId,markTargetScoreMethod.method_pointer,reinterpret_cast<void*>(onMarkTargetScore),reinterpret_cast<void**>(&nextMarkTargetScore),&scoreHandle)!=BE_Result_Ok)
                {std::lock_guard lock(worldMutex);world.requireTargetGate(false);}
        }else {std::lock_guard lock(worldMutex);world.requireTargetGate(false);}
        hooked.store(true);
        BE_MethodDescriptorV1 damage{"Gameplay.Beyond.dll","Beyond.Gameplay.Core","AbilitySystem","ApplyModifier","Beyond.Gameplay.Core.AbilitySystem.Modifier&","Beyond.Gameplay.Core.AbilitySystem.Modifier.ApplyResult",1};
        if(api->resolve_method(api->context,&damage,&damageMethod)==BE_Result_Ok&&damageMethod.method_pointer) {
            std::uint64_t damageHandle=0;damageHooked=host.hooks->create(host.hooks->context,kId,damageMethod.method_pointer,reinterpret_cast<void*>(onDamage),reinterpret_cast<void**>(&nextDamage),&damageHandle)==BE_Result_Ok;
        }
        BE_MethodDescriptorV1 transferred{"Gameplay.Beyond.dll","Beyond.Gameplay.Core","AbilitySystem","ApplyTransferredModifier","Beyond.Gameplay.Core.AbilitySystem.Modifier&|System.Boolean","Beyond.Gameplay.Core.AbilitySystem.Modifier.ApplyResult",2};
        if(api->resolve_method(api->context,&transferred,&transferredMethod)==BE_Result_Ok&&transferredMethod.method_pointer) {
            std::uint64_t transferredHandle=0;transferredHooked=host.hooks->create(host.hooks->context,kId,transferredMethod.method_pointer,reinterpret_cast<void*>(onTransferred),reinterpret_cast<void**>(&nextTransferred),&transferredHandle)==BE_Result_Ok;
        }
        BE_MethodDescriptorV1 immune{"Gameplay.Beyond.dll","Beyond.Gameplay.Core","AbilitySystem","_CheckIsDamageImmune","Beyond.Gameplay.Core.AbilitySystem.Modifier&","System.Boolean",1};
        if(api->resolve_method(api->context,&immune,&immuneMethod)==BE_Result_Ok&&immuneMethod.method_pointer) {
            std::uint64_t immuneHandle=0;immuneHooked=host.hooks->create(host.hooks->context,kId,immuneMethod.method_pointer,reinterpret_cast<void*>(onImmune),reinterpret_cast<void**>(&nextImmune),&immuneHandle)==BE_Result_Ok;
        }
        BE_MethodDescriptorV1 die{"Gameplay.Beyond.dll","Beyond.Gameplay.Core","Entity","Die",nullptr,"System.Void",0};
        if(api->resolve_method(api->context,&die,&dieMethod)==BE_Result_Ok&&dieMethod.method_pointer) {
            std::uint64_t dieHandle=0;dieHooked=host.hooks->create(host.hooks->context,kId,dieMethod.method_pointer,reinterpret_cast<void*>(onDie),reinterpret_cast<void**>(&nextDie),&dieHandle)==BE_Result_Ok;
        }
        BE_MethodDescriptorV1 enemyTarget{"Gameplay.Beyond.dll","Beyond.Gameplay.AI","EnemyAITargetController","_FindTarget","Beyond.Gameplay.AI.EnemyAITargetController.TargetRange&","Beyond.Gameplay.Core.Entity",1};
        BE_MethodDescriptorV1 assignTarget{"Gameplay.Beyond.dll","Beyond.Gameplay.AI","EnemyAITargetController","SetTarget","Beyond.Gameplay.Core.Entity","System.Void",1};
        if(api->resolve_method(api->context,&assignTarget,&assignEnemyTargetMethod)==BE_Result_Ok&&assignEnemyTargetMethod.method_pointer) {
            std::uint64_t assignHandle=0;assignEnemyTargetHooked=host.hooks->create(host.hooks->context,kId,assignEnemyTargetMethod.method_pointer,reinterpret_cast<void*>(onAssignEnemyTarget),reinterpret_cast<void**>(&nextAssignEnemyTarget),&assignHandle)==BE_Result_Ok;
        }
        if(api->resolve_method(api->context,&enemyTarget,&enemyTargetMethod)==BE_Result_Ok&&enemyTargetMethod.method_pointer) {
            std::uint64_t targetHandle=0;enemyTargetHooked=host.hooks->create(host.hooks->context,kId,enemyTargetMethod.method_pointer,reinterpret_cast<void*>(onEnemyTarget),reinterpret_cast<void**>(&nextEnemyTarget),&targetHandle)==BE_Result_Ok;
        }
        BE_MethodDescriptorV1 enableFind{"Gameplay.Beyond.dll","Beyond.Gameplay.AI","EnemyAITargetController","EnableFindTarget",nullptr,"System.Boolean",0};
        if(api->resolve_method(api->context,&enableFind,&enableEnemyFindMethod)==BE_Result_Ok&&enableEnemyFindMethod.method_pointer) {
            std::uint64_t enableHandle=0;enableEnemyFindHooked=host.hooks->create(host.hooks->context,kId,enableEnemyFindMethod.method_pointer,reinterpret_cast<void*>(onEnableEnemyFind),reinterpret_cast<void**>(&nextEnableEnemyFind),&enableHandle)==BE_Result_Ok;
        }
        BE_MethodDescriptorV1 cast{"Gameplay.Beyond.dll","Beyond.Gameplay.Core","AbilitySystem","TryCastSkill",
            "System.String|Beyond.Gameplay.Core.TargetHandleView|System.Action<Beyond.Gameplay.Core.Skill.FinishType,System.Int32>|System.Nullable<Beyond.Gameplay.Core.Skill.CastSkillOptions>|System.Action<Beyond.Gameplay.Core.Skill>|System.Action<Beyond.Gameplay.Core.Skill>|Beyond.Gameplay.Core.Skill.SkillCastInfo&|System.Boolean","System.Boolean",8};
        BE_ResolvedClassV1 view{};auto size=reinterpret_cast<int(*)(const void*,unsigned*)>(GetProcAddress(assembly,"il2cpp_class_value_size"));unsigned align=0;
        auto parameter=reinterpret_cast<const void*(*)(const void*,unsigned)>(GetProcAddress(assembly,"il2cpp_method_get_param"));
        auto classFrom=reinterpret_cast<const void*(*)(const void*)>(GetProcAddress(assembly,"il2cpp_class_from_type"));
        if(size&&api->resolve_class(api->context,"Gameplay.Beyond.dll","Beyond.Gameplay.Core","TargetHandleView",&view)==BE_Result_Ok&&
           size(view.class_info,&align)==int(sizeof(void*))&&api->resolve_method(api->context,&cast,&castMethod)==BE_Result_Ok&&castMethod.method_pointer&&parameter&&classFrom) {
            auto* optionsClass=classFrom(parameter(castMethod.method_info,3));const int optionBytes=optionsClass?size(optionsClass,&align):0;
            if(optionBytes>int(sizeof(void*))&&optionBytes<=4096&&align<=16) {
                std::uint64_t castHandle=0;castHooked=host.hooks->create(host.hooks->context,kId,castMethod.method_pointer,reinterpret_cast<void*>(onCast),reinterpret_cast<void**>(&nextCast),&castHandle)==BE_Result_Ok;
            }
        }
        BE_MethodDescriptorV1 skillEnd{"Gameplay.Beyond.dll","Beyond.Gameplay.Core","Skill","CastEnd",
            "Beyond.Gameplay.Core.Skill.FinishType|Beyond.Gameplay.Core.Skill.InterruptReason|System.Nullable<Beyond.Gameplay.Core.Skill.InterruptContext>","System.Void",3};
        if(api->resolve_method(api->context,&skillEnd,&skillEndMethod)==BE_Result_Ok&&skillEndMethod.method_pointer) {
            auto parameter=reinterpret_cast<const void*(*)(const void*,unsigned)>(GetProcAddress(assembly,"il2cpp_method_get_param"));
            auto fromType=reinterpret_cast<const void*(*)(const void*)>(GetProcAddress(assembly,"il2cpp_class_from_type"));
            auto valueSize=reinterpret_cast<int(*)(const void*,unsigned*)>(GetProcAddress(assembly,"il2cpp_class_value_size"));
            if(parameter&&fromType&&valueSize) {
                auto* type=parameter(skillEndMethod.method_info,2);auto* klass_=type?fromType(type):nullptr;unsigned alignment=0;
                // Nullable<InterruptContext> is passed indirectly on Windows x64.
                if(klass_&&valueSize(klass_,&alignment)==24&&alignment==8) {
                    std::uint64_t endHandle=0;skillEndHooked=host.hooks->create(host.hooks->context,kId,skillEndMethod.method_pointer,reinterpret_cast<void*>(onSkillEnd),reinterpret_cast<void**>(&nextSkillEnd),&endHandle)==BE_Result_Ok;
                }
            }
        }
        {std::lock_guard lock(worldMutex);world.setNativeEvidence(damageHooked&&transferredHooked&&dieHooked&&castHooked,
            officialFilterReady&&opFilterReady&&enemyTargetHooked&&enableEnemyFindHooked&&assignEnemyTargetHooked);
            world.setWeaponLookupEvidence(charInfoHooked&&tryCharInfoHooked&&weaponPathHooked);}
    }
    return result;
}

// ---- configuration ----
std::string defaultName() {
    wchar_t computer[64]{};DWORD size=64;GetComputerNameW(computer,&size);
    std::string raw;for(DWORD i=0;i<size;++i) raw+=char(computer[i]&0x7F);char suffix[8];
    std::snprintf(suffix,sizeof suffix,"%04X",unsigned(eflink::proto::fnv32(raw)&0xFFFF));
    return std::string("管理员-")+suffix;
}
std::string playerName() {
    auto name=configuration.value("player_name",std::string{});
    return name.empty()?defaultName():name;
}
void applyConfiguration(const json& value) {
    eflink::validateSettings(value);
    configuration=value.is_object()?value:json::object();
    net.setName(playerName());
    std::lock_guard lock(worldMutex);
    world.showRemote=configuration.value("show_remote_players",true);
    world.showMarkers=configuration.value("show_player_markers",true);
    session.followHost=configuration.value("follow_host_world",true);
    session.combat=configuration.value("sync_combat",true);
}
eflink::HostOptions hostOptions(const json& input) {
    eflink::HostOptions o;
    o.roomName=input.value("room_name",configuration.value("room_name",std::string{}));
    o.password=input.value("password",configuration.value("room_password",std::string{}));
    o.maxPlayers=input.value("max_players",configuration.value("max_players",8u));
    const auto port=input.value("port",configuration.value("port",unsigned(eflink::proto::kDefaultPort)));
    if(port<1024||port>65535||port==eflink::proto::kDiscoveryPort) throw std::runtime_error("连接端口无效或与发现端口冲突");
    if(o.maxPlayers<2||o.maxPlayers>16||o.roomName.size()>64||o.password.size()>128) throw std::runtime_error("房间参数无效");
    o.port=std::uint16_t(port);
    return o;
}

// ---- status for the page ----
const char* roleName(eflink::Role role) {
    switch(role) {case eflink::Role::Host:return "host";case eflink::Role::Client:return "client";case eflink::Role::Connecting:return "connecting";default:return "offline";}
}
json status(std::uint64_t chatAfter) {
    const auto s=net.snapshot();
    const eflink::proto::PlayerState* self=nullptr;std::uint32_t localMap=0;
    for(const auto& p:s.peers) if(p.local&&!p.states.empty()) {self=&p.states.back().state;localMap=self->mapHash;}
    json peers=json::array();
    for(const auto& p:s.peers) {
        json entry={{"id",p.id},{"name",p.info.name},{"host",p.host},{"local",p.local},{"model",p.info.modelPath},
            {"map",p.info.mapId},{"level",p.info.levelId},{"address",p.address},{"rtt_ms",p.rttMs}};
        if(!p.states.empty()) {
            const auto& st=p.states.back().state;
            entry["position"]={st.position[0],st.position[1],st.position[2]};
            entry["valid"]=(st.flags&eflink::proto::StateValid)!=0;
            entry["same_map"]=localMap&&st.mapHash==localMap;
            entry["state_age_ms"]=eflink::nowMs()-p.states.back().receivedMs;
            if(self&&!p.local&&st.mapHash==localMap) {
                const float dx=st.position[0]-self->position[0],dy=st.position[1]-self->position[1],dz=st.position[2]-self->position[2];
                entry["distance"]=std::sqrt(dx*dx+dy*dy+dz*dz);
            }
        }
        peers.push_back(entry);
    }
    json rooms=json::array();
    for(const auto& r:s.rooms) rooms.push_back({{"room_id",r.beacon.roomId},{"name",r.beacon.roomName},{"host",r.beacon.hostName},
        {"players",r.beacon.players},{"max_players",r.beacon.maxPlayers},{"password",r.beacon.password},{"map",r.beacon.mapId},
        {"version",r.beacon.version},{"address",r.address},{"age_ms",eflink::nowMs()-r.seenMs}});
    json chat=json::array();std::uint64_t last=chatAfter;
    for(const auto& line:net.chatSince(chatAfter)) {
        chat.push_back({{"serial",line.serial},{"from",line.from},{"name",line.name},{"text",line.text},{"system",line.system}});
        last=line.serial;
    }
    json addresses=json::array();
    for(const auto& [ip,adapter]:eflink::localAddresses()) addresses.push_back({{"ip",ip},{"adapter",adapter}});
    json game;
    json worldSession;
    {std::lock_guard lock(worldMutex);game=world.status();worldSession=session.status();}
    return {{"module",kId},{"version",kVersion},{"name",playerName()},{"role",roleName(s.role)},{"local_id",s.localId},
        {"room",{{"id",s.roomId},{"name",s.roomName},{"max_players",s.maxPlayers},{"password",s.password},{"port",s.port},{"address",s.address}}},
        {"rtt_ms",s.rttMs},{"peers",peers},{"rooms",rooms},{"chat",chat},{"chat_serial",last},{"last_error",s.lastError},
        {"addresses",addresses},{"game",game},{"world_session",worldSession},{"room_character_lookup_ready",charInfoHooked&&tryCharInfoHooked&&weaponPathHooked},
        {"player_cast_observer_ready",castHooked&&skillEndHooked},{"damage_hook_ready",damageHooked},{"transferred_hook_ready",transferredHooked},{"immune_hook_ready",immuneHooked},{"death_hook_ready",dieHooked},{"enemy_target_hook_ready",enemyTargetHooked},{"enemy_find_hook_ready",enableEnemyFindHooked},{"skill_hook_ready",castHooked},{"hook_ready",hooked.load()},{"official_filter_ready",officialFilterReady},{"op_filter_ready",opFilterReady},{"op_filter_note",opFilterError},{"outbound",outboundStatus()},{"testing_invincible",world.testingInvincible},{"invincible_blocked",world.invincibleBlocked},{"hook_error",hookError},
        {"show_remote_players",configuration.value("show_remote_players",true)},{"configuration",configuration},
        {"gui_ready",gui.ready()},{"gui_visible",gui.visible()},{"gui_error",guiError},{"gui_backend","unity_canvas_imgui"},{"gui_frames",gui.frames()},{"gui_render",gui.diagnostics()},
        {"traffic",{{"bytes_in",s.bytesIn},{"bytes_out",s.bytesOut},{"states_in",s.statesIn},{"states_out",s.statesOut},{"udp_pongs",s.pongs}}}};
}

// ---- public C API ----
EFLink_Result EFLINK_CALL apiGetSession(EFLink_SessionInfo* out) {
    if(!out||out->struct_size<sizeof(EFLink_SessionInfo)) return EFLink_InvalidArgument;
    const auto s=net.snapshot();
    out->role=EFLink_Role(int(s.role));out->local_id=s.localId;out->player_count=std::uint16_t(s.peers.size());out->room_id=s.roomId;
    strncpy_s(out->room_name,s.roomName.c_str(),_TRUNCATE);strncpy_s(out->local_name,net.name().c_str(),_TRUNCATE);
    return EFLink_Ok;
}
std::uint32_t EFLINK_CALL apiListPeers(EFLink_PeerInfo* out,std::uint32_t capacity) {
    const auto s=net.snapshot();
    std::uint32_t localMap=0;for(const auto& p:s.peers) if(p.local&&!p.states.empty()) localMap=p.states.back().state.mapHash;
    std::uint32_t i=0;
    for(const auto& p:s.peers) {
        if(i>=capacity||!out) break;
        auto& e=out[i];if(e.struct_size<sizeof(EFLink_PeerInfo)) return 0;
        const auto size=e.struct_size;std::memset(&e,0,sizeof e);e.struct_size=size;
        e.id=p.id;e.is_local=p.local;e.is_host=p.host;e.rtt_ms=p.local?s.rttMs:0;
        if(!p.states.empty()) {
            const auto& st=p.states.back().state;e.has_state=eflink::nowMs()-p.states.back().receivedMs<3000;
            e.same_map=localMap&&st.mapHash==localMap;
            for(int k=0;k<3;++k) e.position[k]=st.position[k];
            // Yaw of the model's forward axis (Unity: +Z forward, Y up).
            const float x=st.rotation[0],y=st.rotation[1],z=st.rotation[2],w=st.rotation[3];
            e.yaw_degrees=std::atan2(2*(x*z+w*y),1-2*(x*x+y*y))*57.29578f;
        }
        strncpy_s(e.name,p.info.name.c_str(),_TRUNCATE);strncpy_s(e.map_id,p.info.mapId.c_str(),_TRUNCATE);
        strncpy_s(e.model_path,p.info.modelPath.c_str(),_TRUNCATE);
        ++i;
    }
    return std::uint32_t(s.peers.size());
}
EFLink_Result EFLINK_CALL apiOpenChannel(const char* name,EFLink_ReceiveFn receive,void* user,std::uint32_t* handle) {
    if(!name||!*name||std::strlen(name)>128||!handle) return EFLink_InvalidArgument;
    if(eflink::proto::fnv32(name)==eflink::worldChannel) return EFLink_InvalidArgument;
    std::lock_guard lock(registryMutex);
    for(const auto& [id,c]:channels) if(c.hash==eflink::proto::fnv32(name)&&c.name!=name) return EFLink_Busy;
    const auto id=nextHandle++;channels[id]={name,eflink::proto::fnv32(name),receive,user};*handle=id;return EFLink_Ok;
}
EFLink_Result EFLINK_CALL apiCloseChannel(std::uint32_t handle) {
    std::lock_guard lock(registryMutex);return channels.erase(handle)?EFLink_Ok:EFLink_NotFound;
}
EFLink_Result EFLINK_CALL apiSend(std::uint32_t handle,std::uint16_t to,const void* data,std::uint32_t size,std::uint32_t flags) {
    if((!data&&size)||(flags&~EFLink_Reliable)) return EFLink_InvalidArgument;
    std::uint32_t hash=0;
    {std::lock_guard lock(registryMutex);auto found=channels.find(handle);if(found==channels.end()) return EFLink_NotFound;hash=found->second.hash;}
    const bool reliable=(flags&EFLink_Reliable)!=0;
    if(size>(reliable?eflink::proto::kMaxReliableData:eflink::proto::kMaxUnreliableData)) return EFLink_TooLarge;
    return net.sendData(hash,to,data,size,reliable)?EFLink_Ok:EFLink_NotConnected;
}
EFLink_Result EFLINK_CALL apiAddListener(EFLink_PeerFn callback,void* user,std::uint32_t* handle) {
    if(!callback||!handle) return EFLink_InvalidArgument;
    std::lock_guard lock(registryMutex);const auto id=nextHandle++;listeners[id]={callback,user};*handle=id;return EFLink_Ok;
}
EFLink_Result EFLINK_CALL apiRemoveListener(std::uint32_t handle) {
    std::lock_guard lock(registryMutex);return listeners.erase(handle)?EFLink_Ok:EFLink_NotFound;
}
EFLink_Result EFLINK_CALL apiHost(const char* options) {
    std::lock_guard lock(lifecycle);if(!initialized) return EFLink_NotConnected;
    try {net.host(hostOptions(json::parse(options&&*options?options:"{}")));return EFLink_Ok;}
    catch(...) {return EFLink_InvalidArgument;}
}
EFLink_Result EFLINK_CALL apiJoin(const char* address,const char* password) {
    if(!address||!*address||std::strlen(address)>256||(password&&std::strlen(password)>128)) return EFLink_InvalidArgument;
    std::lock_guard lock(lifecycle);if(!initialized) return EFLink_NotConnected;
    net.join(address,password?password:"");return EFLink_Ok;
}
EFLink_Result EFLINK_CALL apiLeave() {std::lock_guard lock(lifecycle);if(!initialized) return EFLink_NotConnected;net.leave();return EFLink_Ok;}
const EFLink_ApiV1 publicApi{sizeof(EFLink_ApiV1),EFLINK_API_VERSION_1,apiGetSession,apiListPeers,apiOpenChannel,apiCloseChannel,apiSend,
    apiAddListener,apiRemoveListener,apiHost,apiJoin,apiLeave};
std::uint32_t EFLINK_CALL apiGetWorld(char* destination,std::uint32_t capacity) {
    std::lock_guard lock(worldMutex);
    const auto text=json({{"session",session.status()},{"world",session.snapshot()},{"native",world.status().at("world_replication")}}).dump();
    const auto required=std::uint32_t(text.size()+1);if(destination&&capacity>=required) std::memcpy(destination,text.c_str(),required);return required;
}
EFLink_Result EFLINK_CALL apiTeleport(std::uint16_t peer) {
    std::lock_guard lock(lifecycle);if(!initialized) return EFLink_NotConnected;
    std::lock_guard lockWorld(worldMutex);
    try {session.requestTeleport(net,peer);return EFLink_Ok;}catch(...) {return EFLink_InvalidArgument;}
}
const EFLink_ApiV2 worldApi{sizeof(EFLink_ApiV2),EFLINK_API_VERSION_2,&publicApi,apiGetWorld,apiTeleport};

// ---- module lifecycle ----
void reply(const char* request,BE_Result result,const json& body) {
    const auto text=body.dump(-1,' ',false,json::error_handler_t::replace);host.reply(host.context,request,result,text.c_str());
}
// One controller for native GUI, SDK control and optional Host diagnostic messages.
json perform(const json& input) {
    const auto action=input.at("action").get<std::string>();bool save=false;
    if(action=="host") net.host(hostOptions(input));
    else if(action=="join") {
        const auto address=input.at("address").get<std::string>();const auto password=input.value("password",std::string{});
        if(address.empty()||address.size()>256||password.size()>128) throw std::runtime_error("地址或密码无效");net.join(address,password);
    } else if(action=="leave") net.leave();
    else if(action=="host_map_choice") {std::lock_guard lock(worldMutex);world.chooseHostMap(net,input.at("token").get<std::uint64_t>(),input.at("follow").get<bool>());}
    else if(action=="teleport_to_player") {std::lock_guard lock(worldMutex);session.requestTeleport(net,input.at("id").get<std::uint16_t>());}
    else if(action=="outbound_capture") {
        nlohmann::json fed=nlohmann::json::array();
        {std::lock_guard lock(worldMutex);for(auto id:world.fedIds())fed.push_back(id);}
        std::lock_guard lock(outboundMutex);nlohmann::json ops(outboundBattleOps);outboundBattleOps.clear();
        nlohmann::json created=nlohmann::json::array();
        for(const auto& [key,count]:createdOps)created.push_back({{"source",key.first},{"battle_inst",key.second},{"count",count}});
        createdOps.clear();
        return {{"battle_ops",ops},{"created",created},{"fed",fed},{"diagnostic",true}};
    }
    else if(action=="emergency_home") {
        // Leave first; the world teleports once the private world is restored.
        if(net.currentRole()!=eflink::Role::Offline)net.leave();
        std::lock_guard lock(worldMutex);world.requestEmergencyHome();
    }
    else if(action=="testing_invincible") {std::lock_guard lock(worldMutex);world.testingInvincible=input.at("enabled").get<bool>();}
    else if(action=="feed_option") {std::lock_guard lock(worldMutex);world.setFeedOption(input);}
    else if(action=="hud") {std::lock_guard lock(worldMutex);return {{"markers",world.markerSnapshot()}};}
    else if(action=="world_state") {std::lock_guard lock(worldMutex);return {{"session",session.status()},{"world",session.snapshot()},{"native",world.status().at("world_replication")}};}
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    else if(action=="native_owner_probe"||action=="native_travel_probe"||action=="native_self_cast_probe"||action=="native_feed_lifecycle_probe"||action=="enemy_census"||action=="native_move_probe"||action=="meta_search"||action=="send_traces"||action=="teleport_marks"||action=="class_info") {
        std::lock_guard lock(worldMutex);world.queueDiagnostic(input);return {{"queued",true},{"diagnostic",true}};}
    else if(action=="native_skill_probe") {std::lock_guard lock(worldMutex);world.queueNativeCast(input);return {{"queued",true},{"diagnostic",true}};}
#endif
    else if(action=="refresh") net.refresh();
    else if(action=="chat") {auto text=input.at("text").get<std::string>();if(text.size()>1000) throw std::runtime_error("消息过长");net.chat(text);}
    else if(action=="kick") {if(net.currentRole()!=eflink::Role::Host) throw std::runtime_error("只有房主能移出玩家");net.kick(input.at("id").get<std::uint16_t>());}
    else if(action=="set_name") {auto changed=configuration;changed["player_name"]=input.at("name");applyConfiguration(changed);save=true;}
    else if(action=="show_remote") {auto changed=configuration;changed["show_remote_players"]=input.at("enabled");applyConfiguration(changed);save=true;}
    else if(action=="settings") {auto changed=configuration;const auto& patch=input.at("configuration");eflink::validateSettings(patch);for(auto key:{"player_name","room_name","max_players","port","menu_key","show_remote_players","show_player_markers","follow_host_world","sync_combat"}) if(patch.contains(key)) changed[key]=patch[key];applyConfiguration(changed);save=true;}
    else if(action=="save_server"||action=="remove_server") {
        auto address=input.at("address").get<std::string>();if(address.empty()||address.size()>256) throw std::runtime_error("请填写有效地址");
        auto servers=configuration.value("saved_servers",json::array());json updated=json::array();
        for(const auto& s:servers) if(s.value("address",std::string{})!=address) updated.push_back(s);
        if(action=="save_server") updated.push_back({{"address",address},{"name",input.value("name",address)}});
        auto changed=configuration;changed["saved_servers"]=updated;applyConfiguration(changed);save=true;
    } else if(action=="open_gui") gui.open();
    else if(action=="gui_error") {guiError=input.at("error").get<std::string>();if(host.log) host.log(host.context,guiError.c_str());}
    else if(action!="status") throw std::runtime_error("unknown action "+action);
    if(save&&configuration.value("gui_enabled",true)&&!configuration.value("preview_mode",false)) eflink::writeSettings(configuration);
    return status(input.value("chat_after",std::uint64_t(0)));
}
BE_Result BE_CALL initialize(const BE_ThirdPartyHostV1* input,const char* config) {
    std::unique_lock lock(lifecycle);
    if(!input||input->version!=1||input->struct_size<offsetof(BE_ThirdPartyHostV1,emit)+sizeof(input->emit)||
       !input->module_id||std::string(input->module_id)!=kId||!input->platform||std::string(input->platform)!="windows-x64"||!input->reply)
        return BE_Result_ContractMismatch;
    if(initialized) return BE_Result_Conflict;
    try {
        auto parsed=json::parse(config?config:"{}");if(!parsed.is_object()) return BE_Result_InvalidArgument;
        if(parsed.value("gui_enabled",true)&&!parsed.value("preview_mode",false)) {
            try {auto preferences=eflink::readSettings();for(auto it=preferences.begin();it!=preferences.end();++it) parsed[it.key()]=it.value();}
            catch(const std::exception& e) {if(input->log) input->log(input->context,e.what());}
        }
        host={};std::memcpy(&host,input,(std::min)(std::size_t(input->struct_size),sizeof(host)));
        net.version=kVersion;net.onData=deliver;net.onPeer=peerChanged;
        eflink::migrateMenuKey(parsed);
        applyConfiguration(parsed);
        if(!net.start()) {host={};return BE_Result_Failed;}
        cleanupRequested=false;cleanupDone=false;
        hookError.clear();guiError.clear();
#ifdef EFLINK_NATIVE_DIAGNOSTICS
        {std::lock_guard nativeLoggerLock(worldMutex);world.setNativeOperationLogger([](const std::string& message){if(host.log)host.log(host.context,message.c_str());});}
#endif
        hookWorker=std::jthread([](std::stop_token stop) {
            while(!stop.stop_requested()&&!hooked.load()) {
                {std::lock_guard workerLock(lifecycle);if(!initialized) return;const auto r=installHook();if(r!=BE_Result_Ok&&r!=BE_Result_NotReady&&r!=BE_Result_ContractMismatch) hookError="game hook unavailable ("+std::to_string(int(r))+")";}
                for(int i=0;i<20&&!stop.stop_requested();++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        initialized=true;
        if(configuration.value("gui_enabled",true)) gui.start([](const json& in)->json {
            std::lock_guard lock(lifecycle);if(!initialized) return {{"error","模块已停用"}};
            try {return perform(in);} catch(const std::exception& e) {return {{"error",e.what()}};}
        },configuration);
        if(configuration.value("auto_host",false)) net.host(hostOptions(json::object()));
        else if(auto address=configuration.value("auto_join",std::string{});!address.empty()) net.join(address,configuration.value("auto_join_password",std::string{}));
        if(host.log) host.log(host.context,"EF Link ready: press = for the native multiplayer menu");
        return BE_Result_Ok;
    } catch(...) {
        initialized=false;lock.unlock();gui.stop();hookWorker.request_stop();if(hookWorker.joinable()) hookWorker.join();net.stop();
        lock.lock();host={};return BE_Result_InvalidArgument;
    }
}
BE_Result BE_CALL configure(const char* config) {
    std::lock_guard lock(lifecycle);
    if(!initialized) return BE_Result_NotReady;
    try {auto parsed=json::parse(config?config:"{}");if(!parsed.is_object()) return BE_Result_InvalidArgument;eflink::migrateMenuKey(parsed);applyConfiguration(parsed);return BE_Result_Ok;}
    catch(...) {return BE_Result_InvalidArgument;}
}
BE_Result BE_CALL message(const char* request,const char* body) {
    std::lock_guard lock(lifecycle);
    if(!initialized) return BE_Result_NotReady;
    if(!request||!body) return BE_Result_InvalidArgument;
    try {
        reply(request,BE_Result_Ok,perform(json::parse(body)));
        return BE_Result_Ok;
    } catch(const std::exception& e) {reply(request,BE_Result_InvalidArgument,{{"error",e.what()}});return BE_Result_Ok;}
}
void BE_CALL shutdown() {
    {std::lock_guard lock(lifecycle);if(!initialized) return;initialized=false;}
    // Never join the GUI or network while holding lifecycle: callbacks may be finishing.
    gui.stop();
    hookWorker.request_stop();if(hookWorker.joinable()) hookWorker.join();
    net.stop();
    // Remote players are Unity objects: remove them on the game thread, if it is still ticking.
    if(hooked.load()||canvasHooked.load()) {
        cleanupRequested=true;
        for(int i=0;i<50&&!cleanupDone.load();++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if(!cleanupDone.load()&&host.log)host.log(host.context,"EF Link cleanup did not complete before module unload; inspect native restoration diagnostics");
        if(host.hooks&&host.hooks->disable_module) host.hooks->disable_module(host.hooks->context,kId);
        hooked=false;canvasHooked=false;castHooked=false;skillEndHooked=false;damageHooked=false;dieHooked=false;enemyTargetHooked=false;enableEnemyFindHooked=false;assignEnemyTargetHooked=false;officialFilterReady=false;opFilterReady=false;outboundObserverReady=false;transferredHooked=false;immuneHooked=false;charInfoHooked=false;tryCharInfoHooked=false;weaponPathHooked=false;
    }
    {std::lock_guard lock(registryMutex);channels.clear();listeners.clear();}
    std::lock_guard lock(lifecycle);std::lock_guard lockWorld(worldMutex);host={};
}
const BE_ThirdPartyModuleV1 module{sizeof(module),1,kId,initialize,configure,message,shutdown};
}
BE_EXPORT const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1() {return &module;}
extern "C" __declspec(dllexport) const EFLink_ApiV1* EFLINK_CALL EFLink_GetApiV1() {return &publicApi;}
extern "C" __declspec(dllexport) const EFLink_ApiV2* EFLINK_CALL EFLink_GetApiV2() {return &worldApi;}
