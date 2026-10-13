#pragma once
// EF Link game side. Everything here runs on the game thread (CameraManager.TailLateTick):
// it creates independent native room actors and samples local operator presentation.
#include "link_net.h"
#include "link_session.h"
#include "unity_bridge.h"
#include "link_native_identity.h"
#include "link_map_consent.h"
#include "authority/link_native_feed.h"
#include "authority/link_metadata.h"
#include "authority/link_native_op_policy.h"
#include "authority/link_official_view.h"
#include "authority/link_official_appearance.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <functional>
#include <cmath>
#include <optional>

namespace eflink {
// The rejected private-world quarantine/replica adapter must never certify or
// execute shared-world admission. Replaced by the native server adapter in progress.
inline constexpr bool legacyReplicaAdapterAvailable=false;
// Native world feed: the guest's client receives the host world's monsters as
// standard server messages (same server IDs, native create/spawn/remove), and
// its own account monsters leave view natively instead of being hidden.
inline constexpr bool nativeFeedAdapter=true;
inline constexpr bool roomAdapterAvailable=legacyReplicaAdapterAvailable||nativeFeedAdapter;
class World {
    using V3=eflink::unity::V3;using Q=eflink::unity::Quaternion;
    eflink::unity::Api api;
    const BE_HostApiV1* runtime=nullptr;
    HMODULE assembly=nullptr;
    bool resolved=false;
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    std::function<void(const std::string&)> nativeOperationLogger;
    nlohmann::json nativeOperations=nlohmann::json::array();
#endif
    void traceNative(const char* stage) {
#ifdef EFLINK_NATIVE_DIAGNOSTICS
        try {
            auto entry=nlohmann::json{{"at",nowMs()},{"stage",stage}};
            if(nativeOperations.size()>=64)nativeOperations.erase(nativeOperations.begin());
            nativeOperations.push_back(entry);
            if(nativeOperationLogger)nativeOperationLogger("native_operation "+entry.dump());
        }catch(...) {} // Diagnostics cannot change native operation outcomes.
#else
        (void)stage;
#endif
    }
    authority::OfficialAppearance officialAppearance;
    nlohmann::json localAppearanceInfo;
    std::uint64_t lastAppearanceSample=0;
    std::string appearanceModel,appearanceError;
    BE_ResolvedMethodV1 mainCharacter{},entityAlive{},entityCinematic{},entityModel{},modelGo{},modelPath{},
        getTransform{},getPosition{},getRotation{},setPosition{},setRotation{},getLocalRotation{},setLocalRotation{},
        getLocalPosition{},setLocalPosition{},isAlive{},objectName{},setActive{},destroy{},
        childrenOf{},mapIdText{},levelIdText{},instantiate{},handleGet{},pathHash{},behaviourEnabled{},colliderEnabled{};
    const void* worldInfoField=nullptr;
    void* loader=nullptr;std::uint32_t loaderRoot=0;

    // Local player.
    void* character=nullptr;void* model=nullptr;std::uint32_t modelRoot=0;
    struct Bone {void* transform;std::uint32_t root;};
    std::vector<Bone> bones;std::vector<std::string> boneNames;std::uint16_t skeleton=0;
    std::string localModel,mapId,levelId;std::uint32_t mapHash=0;
    std::uint64_t lastSample=0,lastMap=0,lastInvalid=0,samples=0;
    std::string localError;
    bool menuCursor=false,savedVisible=false;int savedLock=0;
    bool backgroundLease=false,backgroundOriginal=false,simulationPaused=false,nativeSceneInitialized=false;
    float nativeTimeScale=1,nativeDeltaTime=0;

    // Remote players.
    struct Avatar {
        std::uint16_t id=0;
        std::string modelPath,name,error;
        void* object=nullptr;void* transform=nullptr;std::uint32_t root=0;
        std::uint16_t skeleton=0xFFFF;
        std::vector<void*> bones;                 // indexed like the sender's bone list
        std::map<std::string,void*> byName;
        bool visible=false,native=false,entityPlaced=false;
        std::uint64_t retryAt=0;
        std::uint32_t poses=0,entityMoves=0;
        V3 entityPosition{};
    };
    std::map<std::uint16_t,Avatar> avatars;
    struct Actor {
        std::uint64_t id=0,epoch=0,retry=0,presentationLife=0;std::uint16_t peer=0;
        std::string model,templateId,error;
        void* entity=nullptr;void* info=nullptr;
        void* movement=nullptr;
        NativeIdentity identity;
        std::uint32_t entityRoot=0,infoRoot=0;
        bool registered=false,inited=false,started=false,aiPaused=false,bound=false,hpInitialized=false,casting=false,abilityTicking=false;
        double hp=0,maxHp=0;std::size_t skillCount=0;
        std::map<std::string,Realm::SkillRule> catalog;
        std::uint64_t teleports=0;
#ifdef EFLINK_NATIVE_DIAGNOSTICS
        nlohmann::json movementDiagnostics=nlohmann::json::object();
        std::uint64_t lastDiagnostics=0;
#endif
        nlohmann::json waiting=nlohmann::json::array();
        nlohmann::json growth;
        nlohmann::json appearance=nlohmann::json::object();
        std::string weaponError;
        bool weaponLoaded=false;
        std::uint64_t weaponReloads=0,weaponRetry=0;
        int weaponState=-1;
    };
    std::map<std::uint16_t,Actor> actors;
    std::map<std::uint16_t,nlohmann::json> actorTravelGrants;
    Session* session=nullptr;
    bool nativeCombatReady=false,nativeIsolationReady=false;
    bool nativeWeaponLookupReady=false;
    std::uint64_t roomCharacterLookups=0;
    struct ObservedPlayerCast {void* skill=nullptr;std::uint32_t root=0,nativeId=0;std::uint64_t actor=0,epoch=0,seen=0;NativeIdentity source;void* entity=nullptr;nlohmann::json event;int pendingFinish=-1;};
    std::map<void*,ObservedPlayerCast> observedPlayerCasts;
    NativeCastBindings nativePlayerCastBindings;
    struct PlayerHitSource {std::uint64_t actor=0,epoch=0,cast=0;int action=0,unit=0;std::string skill;};
    std::uint64_t nativePlayerHitMatched=0,nativePlayerHitMissing=0,nativeHitStrike=0,hitClaimsPending=0;
    nlohmann::json lastPlayerHitSource,lastHitClaim;
    std::string nativePlayerHitError;
    std::uint64_t playerCastSequence=0,playerCastEventsSent=0,playerCastEventsSeen=0;
    nlohmann::json lastPlayerCast;
    struct Enemy {
        void* entity=nullptr;std::uint32_t root=0;
        NativeIdentity identity;
        std::uint64_t targetQueries=0,targetActor=0,enableQueries=0,targetAssignments=0,foreignTargetsRejected=0;int targetRange=0;std::string targetError;
#ifdef EFLINK_NATIVE_DIAGNOSTICS
        nlohmann::json aiDiagnostics=nlohmann::json::object();
#endif
        bool borrowed=false,aiPaused=false,controllerPaused=false,hidden=false,originalActive=true,stateChanged=false;
        double originalHp=0;V3 originalPosition{};
        std::uint64_t mirroredCastAt=0,lastCastCheck=0;
        bool mirroredCasting=false;
        bool visibilityApplied=false,battleTargetDetached=false;
    };
    std::map<std::string,Enemy> enemies;
    // Private account enemies and room replicas have separate lifetimes. Never
    // overwrite an account enemy merely because its level logic ID matches the host.
    std::map<void*,Enemy> privateEnemies;
    bool guestRealm=false,roomHost=false,privateTargetIsolationReady=false;
    HostMapConsent hostMapConsent;
    std::string hostMapChoiceMap,hostMapChoiceLevel;
    std::uint64_t hostMapChoiceRoom=0;
    std::map<std::string,nlohmann::json> hostEnemySeeds;
    struct Spawn {void* data=nullptr;std::uint32_t root=0;std::uint64_t retry=0;std::string error;
        std::uint64_t id=0;std::uint32_t generation=0;bool ownershipEstablished=false,committed=false;
        int pipeline=-1;bool resourcesComplete=false,inAoi=false,preload=false,initialStateApplied=false,started=false;
        bool native=false,nativeDataReady=false;std::uint64_t serverId=0,nativeCheckAt=0;};
    std::map<std::string,Spawn> spawnedEnemies;
    std::uint32_t nextSpawnGeneration=0;
    std::uint64_t lastEnemies=0,lastProjection=0,enemyEpoch=0;
    bool applyingDamage=false;std::string replicationError;
    std::size_t matchedEnemies=0,missingEnemies=0,deferredNativeEnemies=0;
    nlohmann::json markers=nlohmann::json::array();
    // Room entry and exit use the game's own loading pipeline so the official
    // loading panel covers the scene swap, as an official teleport does.
    struct Travel {std::string map,level;V3 position;std::uint64_t until=0;bool started=false;int levelNumber=0;
        bool homeward=false,observedLoading=false;std::uint64_t startedAt=0;
        bool official=false; // an official hub teleport to the target map is in flight
        std::string dungeon; // host's dungeon ID ("" = overworld)
        int dungeonStage=0;  // 1 leaving this account's dungeon, 2 entering the host's
        int inMapLoads=0;    // same-map loads tried (bounded: never loop)
    };
    // Official teleports (the map screen's own GameAction.TeleportToPosition to one
    // of this account's factory hubs). The official server validates the hub.
    struct HubTarget {std::string level,inst;std::uint64_t node=0;V3 position{},rotation{};};
    nlohmann::json officialTeleportStatus=nlohmann::json::object();std::uint64_t officialTeleports=0;
    bool emergencyHomePending=false;
    std::optional<Travel> travel;std::string travelState="idle";
    // A by-reference loading parameter must remain rooted beyond the native
    // call while the asynchronous loading pipeline owns its contents.
    std::uint32_t nativeTravelParameterRoot=0;
    // Where this guest stood before entering the host's world; restored on leave.
    std::optional<Travel> home;
    nlohmann::json travelLifecycle=nlohmann::json::object();
    std::uint64_t completedTeleports=0,damageRequests=0;
    std::uint64_t nativeCasts=0,nativeCastRejected=0;
    nlohmann::json lastNativeCast=nlohmann::json::object();
    nlohmann::json nativeHits=nlohmann::json::array();
    std::uint64_t nativeDeaths=0;
    std::uint64_t guestHitsSent=0,roomHitsApplied=0,playerHitsSent=0,playerHitsTaken=0,enemyCastsSent=0,enemyCastsMirrored=0,enemyCastsRejected=0;
    bool guestGatesReady=false,guestRestrictionsActive=false;std::string guestGateError;
    std::map<std::string,std::uint64_t> blockedGuestActions;
    std::uint64_t damageHookCalls=0,guestHitCandidates=0,guestZeroDamage=0,guestHitSendFailed=0,guestDeathsBlocked=0;
    std::map<std::string,std::uint64_t> guestHitSources; // template of whatever damaged a host monster here
    std::uint64_t lastAppliedRevision=0,lastAppliedEpoch=0,lastHostDiagnostics=0;
    std::string lastDiagnosedEnemy;
    std::uint16_t localPeer=0;
    // Enemy ownership, as in official co-op: an enemy's AI runs on the machine of
    // the player it fights, against that player's real operator (the game's enemy
    // AI is built around a main character). Everyone else drives it as a puppet.
    // The host assigns owners and keeps authority over health and death.
    std::map<std::string,std::uint16_t> owners;                                   // host registry / guest copy
    std::map<std::string,std::pair<bool,std::uint64_t>> engagedReports;           // host: owner says "fighting me"
    std::map<std::string,std::pair<std::uint16_t,std::uint64_t>> hitClaims;       // host: last guest to hit it
    struct Motion {std::deque<std::tuple<std::uint64_t,V3,Q>> samples;std::uint64_t stamp=0,commandAt=0;bool applied=false,walking=false;V3 last{};};
    // Guest puppets driven by native SC_MOVE_OBJECT_MOVE instead of MoveTo/TeleportTo.
    bool nativeMovePuppets=true,nativeMoveNotify=false,hostOwnsAll=true;
    // A native server move places the entity at once, so it is sent every frame
    // (0 ms): 20 Hz placement reads as stepping.
    std::uint64_t nativeMoveIntervalMs=0;
    bool nativeMoveSpeed=true,nativeMoveState=true; // diagnostic toggles
    // ServerMoveEntity steers a monster in combat toward the point rather than
    // placing it; the host's interpolated position is pinned exactly as well.
    bool nativeMovePin=true;std::vector<std::pair<void*,V3>> nativePins;std::uint64_t pinErrors=0;
    // Per-frame motion trace of one guest puppet (diagnostic).
    std::uint64_t traceId=0;int traceFrames=0;nlohmann::json motionTrace=nlohmann::json::array();std::uint64_t traceLastAt=0;int feedScene=0;std::uint64_t lastNativeMove=0,nativeMoveErrors=0;std::string nativeMoveError;
    std::map<std::string,Motion> motions;
    std::uint64_t lastOwnership=0,lastEnemyReport=0,ownershipChanges=0,enemyStatesSent=0,enemyStatesApplied=0,enemyMoves=0,enemyWalks=0,enemySnaps=0;
    bool replayingCast=false;
    // Native feed state (guest): host monsters entered under their server IDs,
    // this account's monsters that left view on join (restored on leave), and
    // official enter/leave messages withheld while in the host's world.
    authority::NativeFeed feed;
    std::map<std::uint64_t,authority::FeedMonster> privateRecords;std::string privateRecordsLevel;
    authority::DeferredOfficialMessages withheldOfficial;
    bool restorationPending=false,recoveryAbort=false;
    std::uint64_t restorationAttempts=0,restoreAwaitOfficial=0;
    bool feedFilterActive=false;std::uint64_t withheldCount=0,feedErrors=0,opsSuppressed=0;std::string feedError;
    bool feedOperating=false; // inside our own injected enter/leave view call
    authority::NativeOpPolicy nativeOpPolicy;std::map<std::uint64_t,std::uint64_t> leakedOwners;
    // Latest official SCENE_MONSTER bytes per monster this account's server sent.
    std::map<std::uint64_t,std::string> officialMonsters;
    std::uint64_t officialRestored=0;
    // Guest in the host world. Level monsters share server IDs across accounts,
    // so the instance UID tells this account's monster from the host's:
    // leavingPrivate = own monsters sent out of view once (still releasing
    // asynchronously); fedInstance = the instance accepted as the host's monster.
    std::map<std::uint64_t,std::uint32_t> leavingPrivate,fedInstance;std::uint64_t privateLeaveSkipped=0,feedWaitsForRelease=0;
    std::vector<std::string> enteringOfficial;std::uint64_t officialForwarded=0; // host's official SCENE_MONSTERs for this guest
    bool detachFedScripts=false; // diagnostic toggle, see officialMonsterForGuest
    std::map<std::string,std::uint64_t> damageCensus; // hook:target class:result
    struct FeedScope {bool& flag;bool previous;explicit FeedScope(bool& f):flag(f),previous(f){flag=true;}~FeedScope(){flag=previous;}};
    void leaveNative(const std::vector<std::uint64_t>& ids) {
        if(ids.empty())return;
        // The native view handler schedules withdrawal on later ticks. Register
        // the IDs before dispatch and keep them after the private world resumes.
        nativeOpPolicy.rememberRetired(ids);
        traceNative("monster.leave.begin");
        FeedScope scope(feedOperating);feed.bind(runtime);feed.leave(ids);
        traceNative("monster.leave.end");
    }
    bool roomEnemy(const std::string& key) const {
        return spawnedEnemies.contains(key)||(nativeFeedAdapter&&roomHost&&enemies.contains(key));
    }
    static std::uint64_t serverIdFromKey(const std::string& key) {
        const auto at=key.rfind(":level:");if(at==std::string::npos)return 0;
        try {return std::stoull(key.substr(at+7));}catch(...) {return 0;}
    }
    authority::FeedMonster recordOf(void* entity) {
        authority::FeedMonster m;
        m.id=api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),entity);
        auto* data=api.call(gm("Entity","get_entityData","","Beyond.Gameplay.Core.BaseEntityData"),entity);
        if(data) {
            m.templateId=text(api.call(gm("BaseEntityData","get_templateId","","System.String"),data));
            const auto p=api.value<V3>(gm("BaseEntityData","get_position","","UnityEngine.Vector3"),data);
            const auto r=api.value<V3>(gm("BaseEntityData","get_rotation","","UnityEngine.Vector3"),data);
            m.position[0]=p.x;m.position[1]=p.y;m.position[2]=p.z;m.rotation[0]=r.x;m.rotation[1]=r.y;m.rotation[2]=r.z;
            m.scene=api.value<int>(gm("BaseEntityData","get_levelIdNum","","System.Int32"),data);
            auto* server=api.call(gm("BaseEntityData","get_serverData","","Beyond.Gameplay.EntityServerData"),data);
            if(server&&classOf(server)=="Beyond.Gameplay.EnemyServerData")m.level=api.value<int>(gm("EnemyServerData","get_level","","System.Int32","Beyond.Gameplay"),server);
        }
        if(auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),entity))
            m.hp=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability);
        // Keep this account's battle identity so the restored monster reports
        // to the player's own official world exactly as before.
        m.battleInstance=api.value<std::uint64_t>(gm("Entity","get_serverBattleInstId","","System.UInt64"),entity);
        m.generation=(std::max)(1u,api.value<std::uint32_t>(gm("Entity","get_serverGeneration","","System.UInt32"),entity));
        return m;
    }
    std::uint64_t lastGuestDiagnostics=0;double localHp=0,localMaxHp=0;
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    std::map<std::string,std::pair<std::uint16_t,std::uint64_t>> ownerPins; // development: forced owner for 30 s
    // Development commands arrive on the RPC thread but touch the game, so they
    // only ever run on the game thread (replication), never in the RPC handler.
    std::deque<nlohmann::json> diagnosticQueue;nlohmann::json diagnosticResults=nlohmann::json::array();
#endif
    std::map<std::string,std::uint64_t> localHitAt;
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    std::optional<nlohmann::json> diagnosticCast;
    nlohmann::json localMovementDiagnostics=nlohmann::json::object();
#endif
    std::uint64_t lastReplication=0,lastCapture=0,lastOverworld=0;std::string enemyMap;bool overworldCached=false;
    std::string hostModeId,hostDungeonId;bool hostInSubGame=false,hostInDungeon=false;
    bool overworldScene() {
        if(nowMs()-lastOverworld<1000)return overworldCached;
        lastOverworld=nowMs();
        auto* mode=api.call(gm("GameInstance","get_mode","","Beyond.Gameplay.Core.BaseGameMode","Beyond.Gameplay"));
        auto* dungeon=api.call(gm("GameInstance","get_dungeonManager","","Beyond.Gameplay.Core.DungeonManager","Beyond.Gameplay"));
        auto* subgame=staticObject("subGameManager");
        hostModeId=mode?text(api.call(gm("BaseGameMode","get_modeId","","System.String"),mode)):std::string{};
        hostInSubGame=subgame&&api.value<bool>(gm("SubGameManager","get_inSubGame","","System.Boolean"),subgame);
        // Open-world challenges run as a sub game/mode on the same map and are
        // part of the shared world; dungeons and other scopes are not (yet).
        hostInDungeon=dungeon&&api.value<bool>(gm("DungeonManager","get_inDungeon","","System.Boolean"),dungeon);
        hostDungeonId=hostInDungeon?text(api.call(gm("DungeonManager","get_curDungeonId","","System.String"),dungeon)):std::string{};
        // Guests follow into a dungeon through the official dungeon entry.
        overworldCached=mode&&((api.value<bool>(gm("GameInstance","get_isMainScope","","System.Boolean","Beyond.Gameplay"))&&mapId.rfind("map",0)==0)||
            (hostInDungeon&&!hostDungeonId.empty()));
        return overworldCached;
    }
    // Main-thread cost of EF Link per game frame; hitches here are felt as stutter.
    struct Perf {double frameMs=0,frameMaxMs=0,scanMs=0,replicationMs=0,remotesMs=0;std::uint64_t windowStart=0;double windowMax=0;
        bool containerScan=true;std::string containerError;} perf;
    static double elapsedMs(std::chrono::steady_clock::time_point since) {
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-since).count();
    }

    template<class T> T symbol(const char* name) {return reinterpret_cast<T>(GetProcAddress(assembly,name));}
    BE_ResolvedMethodV1 m(const char* type,const char* name,const char* params,const char* returns,const char* asm_="UnityEngine.CoreModule.dll",const char* space="UnityEngine") {
        return api.method(type,name,params,returns,asm_,space);
    }
    std::uint64_t resolveRetryAt=0;
    void resolve() {
        if(resolved) return;
        // A game update can remove a method; retry slowly instead of throwing every frame.
        if(nowMs()<resolveRetryAt) throw std::runtime_error(localError.empty()?"game contracts unavailable":localError);
        resolveRetryAt=nowMs()+5000;
        constexpr const char* G="Gameplay.Beyond.dll";
        mainCharacter=m("PlayerController","GetMainCharacter","","Beyond.Gameplay.Core.Entity",G,"Beyond.Gameplay.Core");
        entityAlive=m("Entity","get_alive","","System.Boolean",G,"Beyond.Gameplay.Core");
        entityCinematic=m("Entity","get_inCinematic","","System.Boolean",G,"Beyond.Gameplay.Core");
        entityModel=m("Entity","get_modelCom","","Beyond.Gameplay.View.ModelComponent",G,"Beyond.Gameplay.Core");
        modelGo=m("BaseModelComponent","GetModelGo","","UnityEngine.GameObject",G,"Beyond.Gameplay.View");
        modelPath=m("BaseModelComponent","get_modelPath","","System.String",G,"Beyond.Gameplay.View");
        mapIdText=m("WorldInfo","get_curMapIdStr","","System.String",G,"Beyond.Gameplay.Core");
        levelIdText=m("WorldInfo","get_curLevelId","","System.String",G,"Beyond.Gameplay.Core");
        getTransform=m("GameObject","get_transform","","UnityEngine.Transform");
        getPosition=m("Transform","get_position","","UnityEngine.Vector3");
        getRotation=m("Transform","get_rotation","","UnityEngine.Quaternion");
        setPosition=m("Transform","set_position","UnityEngine.Vector3","System.Void");
        setRotation=m("Transform","set_rotation","UnityEngine.Quaternion","System.Void");
        getLocalRotation=m("Transform","get_localRotation","","UnityEngine.Quaternion");
        setLocalRotation=m("Transform","set_localRotation","UnityEngine.Quaternion","System.Void");
        getLocalPosition=m("Transform","get_localPosition","","UnityEngine.Vector3");
        setLocalPosition=m("Transform","set_localPosition","UnityEngine.Vector3","System.Void");
        isAlive=m("Object","op_Implicit","UnityEngine.Object","System.Boolean");
        objectName=m("Object","get_name","","System.String");
        setActive=m("GameObject","SetActive","System.Boolean","System.Void");
        destroy=m("Object","Destroy","UnityEngine.Object","System.Void");
        childrenOf=m("GameObject","GetComponentsInChildren","System.Type|System.Boolean","UnityEngine.Component[]");
        behaviourEnabled=m("Behaviour","set_enabled","System.Boolean","System.Void");
        colliderEnabled=m("Collider","set_enabled","System.Boolean","System.Void","UnityEngine.PhysicsModule.dll");
        instantiate=m("SimpleAssetLoader","InstantiatePrefab","Beyond.Resource.StringPathHash|System.Boolean","Beyond.Resource.FAssetProxyLoaderHandle","Common.Beyond.dll","Beyond.Resource");
        handleGet=m("FAssetProxyLoaderHandle","Get","","UnityEngine.Object","Common.Beyond.dll","Beyond.Resource");
        pathHash=m("StringPathHash","op_Implicit","System.String","Beyond.Resource.StringPathHash","Common.Beyond.dll","Beyond.Resource");
        auto world=api.klass("GameWorld","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
        worldInfoField=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(world.class_info,"worldInfo");
        if(!worldInfoField) throw std::runtime_error("GameWorld.worldInfo unavailable");
        resolved=true;
    }
    std::string text(void* managed) {
        if(!managed) return {};
        auto length=symbol<int(*)(void*)>("il2cpp_string_length");auto chars=symbol<const wchar_t*(*)(void*)>("il2cpp_string_chars");
        const int n=length(managed);if(n<=0||n>4096) return {};
        const int bytes=WideCharToMultiByte(CP_UTF8,0,chars(managed),n,nullptr,0,nullptr,nullptr);
        std::string out(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,chars(managed),n,out.data(),bytes,nullptr,nullptr);return out;
    }
    bool alive(void* object) {return object&&api.value<bool>(isAlive,nullptr,{object});}
    std::vector<void*> components(void* gameObject,const char* type,const char* asm_="UnityEngine.CoreModule.dll",const char* space="UnityEngine") {
        bool inactive=true;
        auto* list=api.call(childrenOf,gameObject,{api.klass(type,asm_,space).type_object,&inactive});
        std::vector<void*> out;if(!list) return out;
        const auto n=symbol<std::uintptr_t(*)(void*)>("il2cpp_array_length")(list);
        auto** items=reinterpret_cast<void**>(static_cast<unsigned char*>(list)+32);
        for(std::uintptr_t i=0;i<n&&i<4096;++i) out.push_back(items[i]);
        return out;
    }
    std::string classOf(void* object) {
        auto klass=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(object);
        std::string space=symbol<const char*(*)(const void*)>("il2cpp_class_get_namespace")(klass);
        return space+"."+symbol<const char*(*)(const void*)>("il2cpp_class_get_name")(klass);
    }
    std::uint32_t rootOf(void* object) {return runtime->gchandle_new(runtime->context,object,0);}
    void release(std::uint32_t& handle) {if(handle) runtime->gchandle_free(runtime->context,handle);handle=0;}

    BE_ResolvedMethodV1 gm(const char* type,const char* name,const char* params="",const char* returns="System.Void",const char* space="Beyond.Gameplay.Core") {
        return m(type,name,params,returns,"Gameplay.Beyond.dll",space);
    }
    void* staticObject(const char* field) {
        auto klass=api.klass("GameWorld","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
        auto* descriptor=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,field);
        if(!descriptor) throw std::runtime_error(std::string("GameWorld field unavailable: ")+field);
        void* value=nullptr;symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(descriptor,&value);return value;
    }
    template<class T> T enumValue(const char* type,const char* name,const char* space="Beyond.Gameplay.Core",const char* assembly_="Gameplay.Beyond.dll") {
        auto klass=api.klass(type,assembly_,space);
        auto* descriptor=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,name);
        if(!descriptor) throw std::runtime_error(std::string("enum unavailable: ")+type+"."+name);
        T value{};symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(descriptor,&value);return value;
    }
    bool usable(void* entity) {
        return entity&&!api.value<bool>(gm("Entity","get_markReleased","","System.Boolean"),entity);
    }
    NativeIdentity identityOf(void* entity) {
        if(!entity)return {};
        return {api.value<std::uint32_t>(gm("Entity","get_instanceUid","","System.UInt32"),entity),
            api.value<std::uint32_t>(gm("Entity","get_serverGeneration","","System.UInt32"),entity),
            api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),entity),true};
    }
    bool currentEntity(void* entity,const NativeIdentity& identity) {
        return usable(entity)&&identity.matches(identityOf(entity));
    }
    bool currentEnemy(const Enemy& enemy) {return currentEntity(enemy.entity,enemy.identity);}
    bool currentActor(const Actor& actor) {return currentEntity(actor.entity,actor.identity);}
    Enemy trackEnemy(void* entity) {
        Enemy enemy;enemy.entity=entity;enemy.identity=identityOf(entity);enemy.root=rootOf(entity);return enemy;
    }
    bool currentSpawn(const Spawn& spawn) {
        return spawn.data&&spawn.ownershipEstablished&&
            (!spawn.committed||api.value<bool>(gm("BaseEntityData","get_isCommitted","","System.Boolean"),spawn.data))&&
            api.value<std::uint64_t>(gm("BaseEntityData","get_id","","System.UInt64"),spawn.data)==spawn.id&&
            api.value<std::uint32_t>(gm("BaseEntityData","get_serverGeneration","","System.UInt32"),spawn.data)==spawn.generation;
    }
    void releaseSpawn(Spawn& spawn) {
        // A released BaseEntityData may already describe a different pooled node.
        try {if(currentSpawn(spawn))api.call(gm("BaseEntityData","Release"),spawn.data);}catch(...) {}
        release(spawn.root);spawn.data=nullptr;spawn.ownershipEstablished=false;
        spawn.committed=false;
    }
    // A host monster delivered through the native feed lives under its real
    // server ID; it is looked up, never allocated by EF Link.
    void* nativeEntity(std::uint64_t id) {
        auto* container=staticObject("objectContainer");if(!container||!id)return nullptr;
        auto* entity=api.call(gm("ObjectContainer","GetEntityById","System.UInt64","Beyond.Gameplay.Core.Entity"),container,{&id});
        if(!usable(entity)||api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),entity)!=id)return nullptr;
        return entity;
    }
    // Committed native data can legitimately have no entity while outside the
    // client's AOI. Do not reset its spawn pipeline by repeatedly entering view.
    // Confirm the host template and isolated battle identity, not just the ID:
    // level monster IDs can also exist in this account's private world.
    bool nativeDataAccepted(std::uint64_t id,const std::string& expectedTemplate) {
        auto klass=api.klass("EntityManager","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
        auto* field_=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,"s_manager");
        void* manager=nullptr;if(field_)symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(field_,&manager);
        if(!manager||!id||expectedTemplate.empty())return false;
        void* data=nullptr;
        if(!api.value<bool>(gm("EntityManager","TryGetEntityData","System.UInt64|Beyond.Gameplay.Core.BaseEntityData&","System.Boolean"),manager,{&id,&data})||!data)return false;
        eflink::unity::Api::TemporaryRoot dataRoot(runtime,data);
        return api.value<std::uint64_t>(gm("BaseEntityData","get_id","","System.UInt64"),data)==id&&
            api.value<bool>(gm("BaseEntityData","get_isCommitted","","System.Boolean"),data)&&
            api.value<std::uint64_t>(gm("BaseEntityData","get_serverBattleInstId","","System.UInt64"),data)==0&&
            text(api.call(gm("BaseEntityData","get_templateId","","System.String"),data))==expectedTemplate;
    }
    void* spawnEntity(const Spawn& spawn) {
        if(spawn.native)return nativeEntity(spawn.serverId);
        if(!currentSpawn(spawn))return nullptr;
        auto* entity=api.call(gm("BaseEntityData","GetEntity","","Beyond.Gameplay.Core.Entity"),spawn.data);
        if(!usable(entity))return nullptr;
        const auto identity=identityOf(entity);
        if(identity.server!=spawn.id||identity.generation!=spawn.generation||
            api.call(gm("Entity","get_entityData","","Beyond.Gameplay.Core.BaseEntityData"),entity)!=spawn.data)
            throw std::runtime_error("房间实体节点与原生分配身份不匹配");
        return entity;
    }
    std::string entityKey(void* entity) {
        auto* data=api.call(gm("Entity","get_inLevelData","","Beyond.Gameplay.LevelEntityData"),entity);
        if(data) {
            auto klass=api.klass("LevelEntityData","Gameplay.Beyond.dll","Beyond.Gameplay");
            auto* f=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,"levelLogicId");
            std::uint64_t id=0;if(f) symbol<void(*)(void*,const void*,void*)>("il2cpp_field_get_value")(data,f,&id);
            if(id) return levelId+":level:"+std::to_string(id);
        }
        // Native feed: every monster without a level logic ID was spawned at
        // runtime by the host's server (scripts, challenge waves, spawners).
        // Guests create it under the host's server ID, so that ID is its
        // identity. Script slots are not unique: a challenge spawns its whole
        // wave into slot 0 of one script.
        if(nativeFeedAdapter) {
            const auto id=api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),entity);
            return id?levelId+":server:"+std::to_string(id):std::string{};
        }
        // Legacy replica path: matched guest-account entities by script slot.
        const auto script=api.value<std::uint64_t>(gm("Entity","get_parentScriptId","","System.UInt64"),entity);
        const auto slot=api.value<std::uint32_t>(gm("Entity","get_parentScriptSlotId","","System.UInt32"),entity);
        if(script) return levelId+":script:"+std::to_string(script)+":"+std::to_string(slot);
        return {};
    }
    void restoreEnemy(Enemy& e) {
        if(!e.borrowed||!currentEnemy(e)) return;
        if(e.battleTargetDetached) {
            auto* battle=staticObject("battle");
            auto* list=battle?api.call(gm("BattleManager","get_allBattleMarkEntities","","Beyond.UnorderedArray<Beyond.Gameplay.Core.Entity>"),battle):nullptr;
            if(list) {
                eflink::unity::Api::TemporaryRoot listRoot(runtime,list);
                auto* result=collection(list,"Contains",{e.entity});bool contains=false;
                if(result)std::memcpy(&contains,runtime->object_unbox(runtime->context,result),sizeof contains);
                if(!contains)collection(list,"Add",{e.entity});
                e.battleTargetDetached=false;
            }
        }
        auto* ai=api.call(gm("Entity","get_enemyAICom","","Beyond.Gameplay.AI.EnemyAIComponent"),e.entity);
        auto* ctrl=api.call(gm("Entity","get_enemyCtrl","","Beyond.Gameplay.Core.EnemyController"),e.entity);
        auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),e.entity);
        auto* movement=api.call(gm("Entity","get_movementComponent","","Beyond.Gameplay.Core.MovementComponent"),e.entity);
        bool warn=false,skip=true;
        if(e.stateChanged&&ability) api.call(gm("AbilitySystem","SetHpDirectly","System.Double|System.Boolean"),ability,{&e.originalHp,&skip});
        if(e.stateChanged&&movement) api.call(gm("MovementComponent","TeleportTo","UnityEngine.Vector3|System.Boolean"),movement,{&e.originalPosition,&warn});
        if(e.aiPaused&&ai) api.call(gm("TickComponent","ResumeTick"),ai);
        if(e.controllerPaused&&ctrl) api.call(gm("TickComponent","ResumeTick"),ctrl);
        if(e.hidden) {auto* component=api.call(entityModel,e.entity);auto* go=component?api.call(modelGo,component):nullptr;if(go&&alive(go)) api.call(setActive,go,{&e.originalActive});}
        e.borrowed=e.aiPaused=e.controllerPaused=e.hidden=e.stateChanged=false;
    }
    bool restorePrivateWorld() {
        if(!nativeFeedAdapter)return true;
        traceNative("private_world.restore.begin");
        restorationPending=true;++restorationAttempts;
        try {
            FeedScope scope(feedOperating);
            feed.bind(runtime);
            if(!privateRecords.empty()&&privateRecordsLevel==mapId+"/"+levelId) {
                for(auto it=privateRecords.begin();it!=privateRecords.end();) {
                    if(auto official=officialMonsters.find(it->first);official!=officialMonsters.end()) {
                        feed.enterRaw({official->second});++officialRestored;it=privateRecords.erase(it);
                    } else ++it;
                }
                // Only monsters that still carry this account's battle identity are
                // rebuilt; one without it is left for the official server to resend
                // rather than recreated as an unattributed local copy.
                for(auto it=privateRecords.begin();it!=privateRecords.end();) {
                    const auto& record=it->second;
                    if(!record.templateId.empty()&&record.hp>0&&record.battleInstance)feed.enter({record});
                    else ++restoreAwaitOfficial;
                    it=privateRecords.erase(it);
                }
            }
            // Another official level has its own freshly initialized entities.
            privateRecords.clear();privateRecordsLevel.clear();
            // Messages the official server sent meanwhile, in their original order.
            withheldOfficial.replay([&](const auto& type,const auto& bytes){feed.replay(type,bytes);});
        }catch(const std::exception& e) {++feedErrors;feedError=e.what();feedFilterActive=true;return false;}
        restorationPending=recoveryAbort=false;feedFilterActive=false;leavingPrivate.clear();fedInstance.clear();traceNative("private_world.restore.end");return true;
    }
    void dropEnemies() {
        // Host monsters entered by the feed leave view whenever the room's
        // monster set is rebuilt; this account's own monsters only come back
        // when the player actually leaves the host world (restorePrivateWorld).
        if(nativeFeedAdapter) {
            std::vector<std::uint64_t> fedIds;
            for(const auto& [key,s]:spawnedEnemies)if(s.native&&s.serverId)fedIds.push_back(s.serverId);
            if(!fedIds.empty())try {leaveNative(fedIds);}catch(const std::exception& e) {++feedErrors;feedError=e.what();}
        }
        for(auto& [pointer,e]:privateEnemies) {try {restoreEnemy(e);}catch(...) {}release(e.root);}
        privateEnemies.clear();
        privateTargetIsolationReady=false;
        for(auto& [key,e]:enemies) {try {restoreEnemy(e);}catch(...) {}release(e.root);}
        enemies.clear();matchedEnemies=missingEnemies=deferredNativeEnemies=0;lastEnemies=0;
        for(auto& [key,s]:spawnedEnemies)releaseSpawn(s);
        spawnedEnemies.clear();
        hostEnemySeeds.clear();
        owners.clear();engagedReports.clear();hitClaims.clear();motions.clear();
    }
    void pauseEnemy(Enemy& e) {
        if(e.borrowed||!currentEnemy(e)) return;
        auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),e.entity);
        if(!ability) throw std::runtime_error("原生敌人能力组件尚未初始化");
        e.originalHp=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability);
        e.originalPosition=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),e.entity);
        auto* component=api.call(entityModel,e.entity);auto* go=component?api.call(modelGo,component):nullptr;
        if(go&&alive(go)) e.originalActive=api.value<bool>(m("GameObject","get_activeSelf","","System.Boolean"),go);
        // Mark borrowed before changes so a partial bridge failure is still restored.
        e.borrowed=true;
        auto* ai=api.call(gm("Entity","get_enemyAICom","","Beyond.Gameplay.AI.EnemyAIComponent"),e.entity);
        auto* ctrl=api.call(gm("Entity","get_enemyCtrl","","Beyond.Gameplay.Core.EnemyController"),e.entity);
        if(ai&&api.value<bool>(gm("TickComponent","IsTicking","","System.Boolean"),ai)) {api.call(gm("TickComponent","PauseTick"),ai);e.aiPaused=true;}
        if(ctrl&&api.value<bool>(gm("TickComponent","IsTicking","","System.Boolean"),ctrl)) {api.call(gm("TickComponent","PauseTick"),ctrl);e.controllerPaused=true;}
    }
    void excludePrivateBattleTarget(Enemy& e,void* list) {
        auto* result=collection(list,"Contains",{e.entity});bool contains=false;
        if(!result)throw std::runtime_error("无法检查原生寻敌列表");
        std::memcpy(&contains,runtime->object_unbox(runtime->context,result),sizeof contains);
        if(contains) {
            // Only borrow membership of the native target list. Leave entity,
            // ability, AI registration and account lifecycle intact.
            e.battleTargetDetached=true;collection(list,"Remove",{e.entity});
        }
    }
    void clearPrivateBattleTarget(void* battle) {
        auto* target=api.call(gm("BattleManager","get_mainTarget","","Beyond.Gameplay.Core.AbilitySystem"),battle);
        auto* entity=target?api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),target):nullptr;
        if(auto it=privateEnemies.find(entity);it!=privateEnemies.end()&&currentEnemy(it->second)) {
            api.call(gm("BattleManager","ForceResetLockTarget"),battle);
            auto method=gm("BattleManager","SetMainTarget","Beyond.ObjectPtr<Beyond.Gameplay.Core.AbilitySystem>|System.Boolean");
            auto* empty=api.parameterValue(method,0);eflink::unity::Api::TemporaryRoot emptyRoot(runtime,empty);
            bool manual=false;api.call(method,battle,{runtime->object_unbox(runtime->context,empty),&manual});
            ++blockedGuestActions["private_target_cache"];
        }
    }
    // The native container's enemy list. FindObjectsOfType walks every Unity
    // object in the scene and caused a periodic frame hitch while in a room.
    std::optional<std::vector<void*>> containerEnemies() {
        if(!perf.containerScan)return std::nullopt;
        try {
            auto* container=staticObject("objectContainer");if(!container)return std::vector<void*>{};
            auto* list=api.call(gm("ObjectContainer","get_allEnemies","","Beyond.UnorderedList<Beyond.Gameplay.Core.Entity>"),container);
            if(!list)return std::vector<void*>{};
            eflink::unity::Api::TemporaryRoot listRoot(runtime,list);
            auto* countBox=collection(list,"get_count");auto* countRaw=countBox?runtime->object_unbox(runtime->context,countBox):nullptr;
            if(!countRaw)throw std::runtime_error("enemy list count unavailable");
            int count=0;std::memcpy(&count,countRaw,sizeof count);
            if(count<0||count>20000)throw std::runtime_error("enemy list count invalid");
            std::vector<void*> out;out.reserve(std::size_t(count));
            for(int i=0;i<count;++i)if(auto* entity=collection(list,"get_Item",{&i}))out.push_back(entity);
            return out;
        }catch(const std::exception& e) {perf.containerScan=false;perf.containerError=e.what();return std::nullopt;}
    }
    std::vector<void*> sceneEnemies() {
        if(auto listed=containerEnemies())return std::move(*listed);
        // Fallback for a game build without the container list contract.
        auto klass=api.klass("ObjectMono","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
        bool inactive=true;
        auto* array=api.call(m("Object","FindObjectsOfType","System.Type|System.Boolean","UnityEngine.Object[]"),nullptr,{klass.type_object,&inactive});
        std::vector<void*> out;if(!array) return out;
        eflink::unity::Api::TemporaryRoot arrayRoot(runtime,array);
        auto n=(std::min)(std::uintptr_t(20000),symbol<std::uintptr_t(*)(void*)>("il2cpp_array_length")(array));
        auto** items=reinterpret_cast<void**>(static_cast<unsigned char*>(array)+32);
        for(std::uintptr_t i=0;i<n;++i)if(items[i])if(auto* entity=api.call(gm("ObjectMono","get_entity","","Beyond.Gameplay.Core.Entity"),items[i]))out.push_back(entity);
        return out;
    }
    void scanEnemies() {
        privateTargetIsolationReady=false;
        const auto started=std::chrono::steady_clock::now();
        const auto entities=sceneEnemies();
        void* battle=nullptr;void* battleList=nullptr;
        std::vector<std::uint64_t> leaving;std::set<std::uint64_t> fed;
        if(nativeFeedAdapter&&guestRealm)for(const auto& [key,s]:spawnedEnemies)if(s.native)fed.insert(s.serverId);
        if(guestRealm&&!nativeFeedAdapter) {
            battle=staticObject("battle");
            battleList=battle?api.call(gm("BattleManager","get_allBattleMarkEntities","","Beyond.UnorderedArray<Beyond.Gameplay.Core.Entity>"),battle):nullptr;
            if(!battleList)throw std::runtime_error("原生寻敌列表尚未就绪");
        }
        std::optional<eflink::unity::Api::TemporaryRoot> battleListRoot;
        if(battleList)battleListRoot.emplace(runtime,battleList);
        std::set<std::string> present;std::set<void*> privatePresent;
        for(const auto& [key,s]:spawnedEnemies) if(auto it=enemies.find(key);it!=enemies.end()&&currentEnemy(it->second)) present.insert(key);
        for(std::size_t i=0;i<entities.size()&&present.size()<512&&privatePresent.size()<512;++i) {
            auto* entity=entities[i];
            if(!usable(entity)||entity==character||!api.call(gm("Entity","get_enemyCtrl","","Beyond.Gameplay.Core.EnemyController"),entity)) continue;
            if(nativeFeedAdapter&&guestRealm) {
                // This account's monster: record it, then let it leave view through
                // the client's own handler. Host monsters entered by the feed stay.
                const auto id=api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),entity);
                if(!id)continue;
                const auto instance=identityOf(entity).instance;
                if(auto f=fedInstance.find(id);f!=fedInstance.end()&&f->second==instance)continue; // the host's monster
                // Already sent out of view: it is releasing; a second leave makes
                // the client release the same entity twice.
                if(auto l=leavingPrivate.find(id);l!=leavingPrivate.end()&&l->second==instance) {++privateLeaveSkipped;continue;}
                if(!privateRecords.contains(id)&&privateRecords.size()<1024)privateRecords.emplace(id,recordOf(entity));
                privateRecordsLevel=mapId+"/"+levelId;leaving.push_back(id);leavingPrivate[id]=instance;continue;
            }
            if(guestRealm) {
                bool owned=false;
                for(const auto& [key,s]:spawnedEnemies) if(spawnEntity(s)==entity) {owned=true;break;}
                if(owned) continue;
                if(roomHost) {
                    const auto key=entityKey(entity);
                    if(!key.empty()&&!hostEnemySeeds.contains(key)&&hostEnemySeeds.size()<512) {
                        auto seed=enemyState(entity,key);if(!seed.empty())hostEnemySeeds.emplace(key,std::move(seed));
                    }
                }
                privatePresent.insert(entity);auto& original=privateEnemies[entity];
                if(!currentEnemy(original)) {release(original.root);original=trackEnemy(entity);}
                pauseEnemy(original);
                excludePrivateBattleTarget(original,battleList);
                auto* component=api.call(entityModel,entity);auto* go=component?api.call(modelGo,component):nullptr;
                if(go&&alive(go)) {bool visible=false;api.call(setActive,go,{&visible});original.hidden=true;}
                continue;
            }
            auto key=entityKey(entity);if(key.empty()||!present.insert(key).second) continue;
            auto& e=enemies[key];if(e.entity==entity&&currentEnemy(e)) continue;
            if(e.root) {try {restoreEnemy(e);}catch(...) {}release(e.root);}
            e=trackEnemy(entity);
        }
        for(auto it=enemies.begin();it!=enemies.end();) {
            if(present.contains(it->first)) {++it;continue;}
            try {restoreEnemy(it->second);}catch(...) {}release(it->second.root);it=enemies.erase(it);
        }
        for(auto it=privateEnemies.begin();it!=privateEnemies.end();) {
            if(privatePresent.contains(it->first)) {++it;continue;}
            try {restoreEnemy(it->second);}catch(...) {}release(it->second.root);it=privateEnemies.erase(it);
        }
        if(!leaving.empty())try {leaveNative(leaving);}
            catch(const std::exception& e) {++feedErrors;feedError=e.what();throw;}
        if(battle)clearPrivateBattleTarget(battle);
        privateTargetIsolationReady=true;
        perf.scanMs=elapsedMs(started);
    }
    nlohmann::json enemyState(void* entity,const std::string& key) {
            if(!usable(entity))return nlohmann::json::object();
            if(!api.value<bool>(gm("Entity","get_hasStarted","","System.Boolean"),entity))return nlohmann::json::object();
            auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),entity);if(!ability)return nlohmann::json::object();
            auto pos=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),entity);
            auto* component=api.call(entityModel,entity);auto* go=component?api.call(modelGo,component):nullptr;
            Q rotation{0,0,0,1};if(go&&alive(go)) rotation=api.value<Q>(getRotation,api.call(getTransform,go));
            double hp=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability),max=api.value<double>(gm("AbilitySystem","get_maxHp","","System.Double"),ability);
            if(!std::isfinite(hp)||!std::isfinite(max)||max<=0)return nlohmann::json::object();
            auto* data=api.call(gm("Entity","get_entityData","","Beyond.Gameplay.Core.BaseEntityData"),entity);
            auto templateId=data?text(api.call(gm("BaseEntityData","get_templateId","","System.String"),data)):std::string{};
            int enemyLevel=1;auto* server=data?api.call(gm("BaseEntityData","get_serverData","","Beyond.Gameplay.EntityServerData"),data):nullptr;
            if(server&&classOf(server)=="Beyond.Gameplay.EnemyServerData")
                enemyLevel=api.value<int>(gm("EnemyServerData","get_level","","System.Int32","Beyond.Gameplay"),server);
            const auto allocation=identityOf(entity);
            return {{"key",key},{"kind","enemy"},{"position",{pos.x,pos.y,pos.z}},{"rotation",{rotation.x,rotation.y,rotation.z,rotation.w}},
                {"native_spawn",(std::uint64_t(allocation.instance)<<32)|allocation.generation},{"poise",readPoise(ability)},
                {"template",templateId},{"enemy_level",enemyLevel},{"server_id",api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),entity)},
                {"battle_inst",api.value<std::uint64_t>(gm("Entity","get_serverBattleInstId","","System.UInt64"),entity)},{"hp",(std::max)(0.,hp)},{"max_hp",max},{"alive",api.value<bool>(entityAlive,entity)}};
    }
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    // Development: read-only state of a room enemy's native AI.
    void diagnoseAI(const std::string& key,void* entity) {
        try {
            auto& enemy=enemies.at(key);auto* ai=api.call(gm("Entity","get_enemyAICom","","Beyond.Gameplay.AI.EnemyAIComponent"),entity);
            constexpr const char* space="Beyond.Gameplay.AI";
            auto* brain=ai?api.call(gm("EnemyAIComponent","get_aiBrain","","Beyond.Gameplay.AI.EnemyAIBrain",space),ai):nullptr;
            auto* controller=brain?api.call(gm("EnemyAIBrain","get_targetController","","Beyond.Gameplay.AI.EnemyAITargetController",space),brain):nullptr;
            auto* current=controller?api.call(gm("EnemyAITargetController","GetTargetNoCache","","Beyond.Gameplay.Core.Entity",space),controller):nullptr;
            std::uint64_t target=0;for(const auto& [peer,actor]:actors)if(actor.entity==current&&currentActor(actor)){target=actor.id;break;}
            auto* mode=brain?api.call(gm("EnemyAIBrain","get_curMode","","Beyond.Gameplay.AI.EnemyAIMode",space),brain):nullptr;
            enemy.aiDiagnostics={{"ai_component",ai!=nullptr},{"brain",brain!=nullptr},{"controller",controller!=nullptr},{"target_actor",target},{"mode",mode?classOf(mode):std::string{}}};
            const auto position=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),entity);
            enemy.aiDiagnostics["position"]={position.x,position.y,position.z};
            if(current) {
                const auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),current);
                enemy.aiDiagnostics["target_position"]={p.x,p.y,p.z};
            }
            if(mode) {
                auto* boxed=collection(mode,"get_modeType");auto* raw=boxed?runtime->object_unbox(runtime->context,boxed):nullptr;
                if(raw) {
                    const int type=*static_cast<int*>(raw);enemy.aiDiagnostics["mode_type"]=type;
                    for(const char* name:{"None","Idle","Patrol","Vigilance","Battle","LeaveBattle","Born","Atmospheric","AttackBuilding","ScriptedMove","AvoidCombat"})
                        if(type==enumValue<int>("EnemyAIModeType",name,space)){enemy.aiDiagnostics["mode_name"]=name;break;}
                }
                auto* graph=collection(mode,"get_graph");
                if(graph) {
                    enemy.aiDiagnostics["graph_pause"]=api.value<bool>(gm("AIGraph","get_pause","","System.Boolean",space),graph);
                    enemy.aiDiagnostics["graph_tick_enabled"]=readField<bool>(graph,"AIGraph","m_enableTick",space);
                    enemy.aiDiagnostics["in_graph"]=readField<bool>(graph,"AIGraph","m_inGraph",space);
                    auto* behaviours=api.call(gm("AIGraph","get_curBehaviors","","System.Collections.Generic.List<Beyond.Gameplay.AI.AIBehavior>",space),graph);
                    auto* count=behaviours?collection(behaviours,"get_Count"):nullptr;
                    auto* countRaw=count?runtime->object_unbox(runtime->context,count):nullptr;
                    enemy.aiDiagnostics["behaviours"]=nlohmann::json::array();
                    if(countRaw)for(int i=0,n=(std::clamp)(*static_cast<int*>(countRaw),0,32);i<n;++i)
                        if(auto* behaviour=collection(behaviours,"get_Item",{&i});behaviour)enemy.aiDiagnostics["behaviours"].push_back(classOf(behaviour));
                }
            }
            if(ai) {
                enemy.aiDiagnostics["ticking"]=api.value<bool>(gm("TickComponent","IsTicking","","System.Boolean"),ai);
                enemy.aiDiagnostics["active_lod"]=readField<bool>(ai,"EnemyAIComponent","m_activeAILod",space);
                enemy.aiDiagnostics["lod"]=readField<int>(ai,"EnemyAIComponent","m_aiLod",space);
                enemy.aiDiagnostics["init_state"]=readField<int>(ai,"EnemyAIComponent","m_initState",space);
                enemy.aiDiagnostics["tick_time"]=readField<float>(ai,"EnemyAIComponent","m_tickTime",space);
            }
            if(brain) {
                enemy.aiDiagnostics["brain_inited"]=readField<bool>(brain,"EnemyAIBrain","m_inited",space);
                const auto born=api.value<V3>(gm("EnemyAIBrain","GetBornPos","","UnityEngine.Vector3",space),brain);
                enemy.aiDiagnostics["born_position"]={born.x,born.y,born.z};
            }
            if(auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),entity);ability)
                enemy.aiDiagnostics["casting"]=anySkillCasting(ability);
            if(controller)enemy.aiDiagnostics["find_enabled"]=api.value<bool>(gm("EnemyAITargetController","EnableFindTarget","","System.Boolean",space),controller);
        }catch(const std::exception& e){enemies.at(key).aiDiagnostics={{"error",e.what()}};}
    }
#endif

#ifdef EFLINK_NATIVE_DIAGNOSTICS
    void sampleHostDiagnostic() {
        if(nowMs()-lastHostDiagnostics<250||enemies.empty())return;
        lastHostDiagnostics=nowMs();
        auto it=enemies.upper_bound(lastDiagnosedEnemy);if(it==enemies.end())it=enemies.begin();
        lastDiagnosedEnemy=it->first;if(currentEnemy(it->second))diagnoseAI(it->first,it->second.entity);
    }
#endif
    nlohmann::json captureWorld() {
        auto* info=staticObject("worldInfo");auto* levelLoader=staticObject("levelLoader");
        if(!info||!character) return nlohmann::json::object();
        auto position=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
        nlohmann::json entities=nlohmann::json::array();
        for(const auto& [key,e]:enemies) {
            if(!currentEnemy(e))continue;
            auto state=enemyState(e.entity,key);if(state.empty())continue;
            // Death is a room lifetime event. A later pool reset/positive HP read
            // must not overwrite the confirmed tombstone for this spawn.
            if(roomHost)if(auto old=hostEnemySeeds.find(key);old!=hostEnemySeeds.end()&&!old->second.value("alive",true)&&
                old->second.value("native_spawn",std::uint64_t(0))==state.value("native_spawn",std::uint64_t(0)))continue;
            // Runtime-spawned monsters (scripts, challenges) need their official
            // server data on the guest; level monsters are completed from level data.
            if(roomHost&&nativeFeedAdapter&&key.find(":server:")!=std::string::npos)
                if(auto official=officialMonsters.find(state.value("server_id",std::uint64_t(0)));official!=officialMonsters.end()&&official->second.size()<=16384)
                    state["official"]=authority::NativeFeed::encode64(official->second);
            if(roomHost)hostEnemySeeds[key]=state;else entities.push_back(std::move(state));
        }
        if(roomHost)for(const auto& [key,seed]:hostEnemySeeds) {
            // Natively unloaded host monsters are not published; tombstones are.
            if(nativeFeedAdapter&&!enemies.contains(key)&&seed.value("alive",true))continue;
            auto item=seed;item["owner"]=ownerOf(key);entities.push_back(std::move(item));
        }
        overworldScene();
        return {{"map",mapId},{"level",levelId},{"dungeon",hostDungeonId},{"level_num",api.value<int>(gm("WorldInfo","get_curLevelIdNum","","System.Int32"),info)},
            {"loading",levelLoader&&api.value<bool>(gm("GameLevelLoader","get_isLoading","","System.Boolean"),levelLoader)},
            {"position",{position.x,position.y,position.z}},{"entities",entities}};
    }
    template<class T> void field(void* object,const char* type,const char* name,T value,const char* space="Beyond.Gameplay.Core") {
        auto klass=api.klass(type,"Gameplay.Beyond.dll",space);
        auto* descriptor=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,name);
        if(!descriptor) throw std::runtime_error(std::string("field unavailable: ")+type+"."+name);
        symbol<void(*)(void*,const void*,void*)>("il2cpp_field_set_value")(object,descriptor,&value);
    }
    template<class T> T readField(void* object,const char* type,const char* name,const char* space="Beyond.Gameplay.Core",const char* library="Gameplay.Beyond.dll") {
        auto klass=api.klass(type,library,space);
        auto* descriptor=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,name);
        if(!descriptor) throw std::runtime_error(std::string("field unavailable: ")+type+"."+name);
        T value{};symbol<void(*)(void*,const void*,void*)>("il2cpp_field_get_value")(object,descriptor,&value);return value;
    }
    void* collection(void* object,const char* name,std::initializer_list<void*> args={}) {
        auto* klass=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(object);
        auto* info=symbol<const void*(*)(const void*,const char*,int)>("il2cpp_class_get_method_from_name")(klass,name,int(args.size()));
        if(!info) throw std::runtime_error(std::string("collection method unavailable: ")+name);
        BE_ResolvedMethodV1 method{};method.method_info=info;return api.call(method,object,args);
    }
    bool resourcesReady(void* node,std::string& reason) {
        // Completed orders may already have been retired from ResourceFactory.
        // The node's own readiness set is the source of truth, not order existence.
        if(api.value<bool>(gm("EntityManager/EntityNode","get_allResourcesReady","","System.Boolean"),node)) return true;
        auto* list=api.call(gm("EntityManager/EntityNode","get_resRequirements","","System.Collections.Generic.List<Beyond.Gameplay.Core.EntityManager.ResourceRequirement>"),node);
        if(!list) {reason="资源需求列表为空";return false;}
        eflink::unity::Api::TemporaryRoot listRoot(runtime,list);
        auto* boxedCount=collection(list,"get_Count");if(!boxedCount) return false;
        int count=0;std::memcpy(&count,runtime->object_unbox(runtime->context,boxedCount),sizeof count);
        if(count<=0||count>256) {reason="资源需求数量无效";return false;}
        auto klass=api.klass("EntityManager/ResourceFactory","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
        auto* descriptor=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,"s_factory");
        if(!descriptor) return false;
        void* factory=nullptr;symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(descriptor,&factory);if(!factory) return false;
        for(int i=0;i<count;++i) {
            auto* item=collection(list,"get_Item",{&i});if(!item) return false;
            eflink::unity::Api::TemporaryRoot itemRoot(runtime,item);
            int type=readField<int>(item,"EntityManager/ResourceRequirement","type");auto* path=readField<void*>(item,"EntityManager/ResourceRequirement","path");void* order=nullptr;
            if(!api.value<bool>(gm("EntityManager/ResourceFactory","TryGetOrder","Beyond.Gameplay.Core.EntityManager.ResourceType|System.String|Beyond.Gameplay.Core.EntityManager.ResourceFactory.ResourceOrder&","System.Boolean"),factory,{&type,path,&order})||!order) {reason="资源订单不存在: "+std::to_string(type)+" "+text(path);return false;}
            // ResourceOrder's getter is abstract. Invoke the actual order subclass,
            // rather than resolving an abstract body through the native hook resolver.
            auto* boxedReady=collection(order,"get_isReady");auto* rawReady=boxedReady?runtime->object_unbox(runtime->context,boxedReady):nullptr;
            bool ready=false;if(rawReady) std::memcpy(&ready,rawReady,sizeof ready);if(!ready) {reason="资源订单未完成: "+std::to_string(type)+" "+text(path);return false;}
            auto* requirement=runtime->object_unbox(runtime->context,item);if(!requirement) return false;
            api.call(gm("EntityManager/EntityNode","MarkResourceReady","Beyond.Gameplay.Core.EntityManager.ResourceRequirement&"),node,{requirement});
        }
        return true;
    }

    void applyWorld(const nlohmann::json& snapshot) {
        matchedEnemies=missingEnemies=deferredNativeEnemies=0;
        std::set<std::string> present;for(const auto& item:snapshot.at("entities")) present.insert(item.at("key").get<std::string>());
        std::vector<std::uint64_t> gone;
        for(auto it=spawnedEnemies.begin();it!=spawnedEnemies.end();) {
            if(present.contains(it->first)) {++it;continue;}
            if(auto bound=enemies.find(it->first);bound!=enemies.end()) {release(bound->second.root);enemies.erase(bound);}
            if(it->second.native&&it->second.serverId)gone.push_back(it->second.serverId);
            releaseSpawn(it->second);
            it=spawnedEnemies.erase(it);
        }
        std::vector<authority::FeedMonster> entering;
        if(nativeFeedAdapter) {
            feed.bind(runtime);feedFilterActive=true;
            // Host monsters that died leave view natively instead of being hidden.
            for(const auto& item:snapshot.at("entities"))if(item.at("hp").get<double>()<=0||!item.value("alive",true)) {
                const auto key=item.at("key").get<std::string>();
                if(auto s=spawnedEnemies.find(key);s!=spawnedEnemies.end()&&s->second.native) {
                    gone.push_back(s->second.serverId);
                    if(auto bound=enemies.find(key);bound!=enemies.end()) {release(bound->second.root);enemies.erase(bound);}
                    spawnedEnemies.erase(s);
                }
            }
            if(!gone.empty())try {leaveNative(gone);}catch(const std::exception& e) {++feedErrors;feedError=e.what();}
        }
        for(const auto& item:snapshot.at("entities")) {
            auto found=enemies.find(item.at("key").get<std::string>());
            if(found==enemies.end()||!currentEnemy(found->second)) {
                if(item.at("hp").get<double>()<=0||!item.value("alive",true)) continue;
                feedScene=snapshot.at("level_num").get<int>();
                if(auto* created=feedEnemy(item,snapshot.at("level_num").get<int>(),entering)) {
                    auto key=item.at("key").get<std::string>();auto& entry=enemies[key];release(entry.root);entry=trackEnemy(created);found=enemies.find(key);
                } else {
                    const auto spawn=spawnedEnemies.find(item.at("key").get<std::string>());
                    if(nativeFeedAdapter&&spawn!=spawnedEnemies.end()&&spawn->second.nativeDataReady)++deferredNativeEnemies;
                    else ++missingEnemies;
                    continue;
                }
            }
            auto& e=found->second;auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),e.entity);
            if(!api.value<bool>(gm("Entity","get_hasStarted","","System.Boolean"),e.entity)) {
                if(api.value<bool>(gm("Entity","get_hasInited","","System.Boolean"),e.entity))api.call(gm("Entity","TryStart"),e.entity);
                if(!api.value<bool>(gm("Entity","get_hasStarted","","System.Boolean"),e.entity)){++missingEnemies;continue;}
            }
            if(auto spawn=spawnedEnemies.find(item.at("key").get<std::string>());spawn!=spawnedEnemies.end()) {
                spawn->second.started=true;
                if(spawn->second.native) {spawn->second.nativeDataReady=true;spawn->second.error.clear();}
            }
            auto* movement=api.call(gm("Entity","get_movementComponent","","Beyond.Gameplay.Core.MovementComponent"),e.entity);
            if(!ability||!movement) {++missingEnemies;continue;}
            pauseEnemy(e);
            e.stateChanged=true;
            const auto key=item.at("key").get<std::string>();
            const auto owner=item.value("owner",std::uint16_t(proto::kHostId));
            if(ownerOf(key)!=owner) {motions[key].samples.clear();motions[key].applied=false;}
            owners[key]=owner;
            // Our own enemy: its native AI fights this player's operator here.
            // Anyone else's: a puppet drawn from the host's 10 Hz snapshots.
            setEnemyAI(e,false);
            bool skip=true;
            if(owner!=localPeer&&item.contains("rotation")) {
                const auto& r=item["rotation"];
                pushMotion(key,session?session->snapshotTime():nowMs(),{item["position"][0].get<float>(),item["position"][1].get<float>(),item["position"][2].get<float>()},
                    {r[0].get<float>(),r[1].get<float>(),r[2].get<float>(),r[3].get<float>()});
            }
            // State projection is suspended until a verified native result/apply
            // adapter exists. Never approximate death by hiding a living entity,
            // copy local HP/poise, or retain a locally resolved guest hit.
            ++matchedEnemies;
        }
        if(!entering.empty())try {FeedScope scope(feedOperating);feed.enter(entering);}catch(const std::exception& e) {++feedErrors;feedError=e.what();}
        if(!enteringOfficial.empty())try {FeedScope scope(feedOperating);feed.enterRaw(enteringOfficial);}catch(const std::exception& e) {++feedErrors;feedError=e.what();}
        enteringOfficial.clear();
    }
    // Ask the client to create a host monster under its real server ID. Returns
    // the native entity once the game's own pipeline has spawned it.
    void* feedEnemy(const nlohmann::json& item,int levelNumber,std::vector<authority::FeedMonster>& entering) {
        const auto key=item.at("key").get<std::string>();
        const std::uint64_t id=item.value("server_id",serverIdFromKey(key));
        if(!id)return nullptr; // script-slot entities have no stable server ID yet
        auto& s=spawnedEnemies[key];s.native=true;s.serverId=id;
        nativeOpPolicy.rememberFed(id);
        if(auto* entity=nativeEntity(id)) {
            const auto instance=identityOf(entity).instance;
            // This account's monster with the same ID is still releasing: wait.
            if(auto l=leavingPrivate.find(id);l!=leavingPrivate.end()&&l->second==instance) {++feedWaitsForRelease;s.error="waiting for private release";return nullptr;}
            if(auto f=fedInstance.find(id);f==fedInstance.end()||f->second!=instance) {
                // Only an instance created after our own enter counts as the host's.
                if(!s.committed) {s.error="unexpected instance with host ID";return nullptr;}
                fedInstance[id]=instance;
            }
            s.started=api.value<bool>(gm("Entity","get_hasStarted","","System.Boolean"),entity);s.nativeDataReady=true;s.error.clear();return entity;
        }
        s.started=false;
        const auto now=nowMs();
        if(now>=s.nativeCheckAt) {
            s.nativeCheckAt=now+250;
            s.nativeDataReady=nativeDataAccepted(id,item.value("template",std::string{}));
        }
        if(s.nativeDataReady) {s.error.clear();return nullptr;}
        if(now<s.retry)return nullptr;s.retry=now+5000;
        if(item.contains("official"))try {
            float position[3];for(int i=0;i<3;++i)position[i]=item.at("position")[i].get<float>();
            enteringOfficial.push_back(authority::officialMonsterForGuest(authority::NativeFeed::decode64(item.at("official").get<std::string>()),position,item.at("hp").get<double>(),detachFedScripts));
            ++officialForwarded;s.committed=true;s.error="awaiting native spawn";return nullptr;
        }catch(const std::exception& e) {s.error=std::string("official data rejected: ")+e.what();}
        authority::FeedMonster m;m.id=id;m.templateId=item.value("template",std::string{});
        if(m.templateId.empty()) {s.error="host monster template missing";return nullptr;}
        for(int i=0;i<3;++i)m.position[i]=item.at("position")[i].get<float>();
        if(item.contains("rotation")) {float q[4];for(int i=0;i<4;++i)q[i]=item["rotation"][i].get<float>();authority::eulerDegrees(q,m.rotation);}
        m.scene=levelNumber;m.hp=item.at("hp").get<double>();m.level=item.value("enemy_level",1);
        entering.push_back(std::move(m));s.committed=true;s.error="awaiting native spawn";
        return nullptr;
    }
    static std::string operatorTemplate(const std::string& path) {
        return characterTemplate(path);
    }
    nlohmann::json infoGrowth(void* info,const std::string& modelPath_) {
        if(!info)return nullptr;
        auto result=nlohmann::json{{"template",text(readField<void*>(info,"CharInfo","templateId","Beyond.Gameplay"))},
            {"level",readField<int>(info,"CharInfo","level","Beyond.Gameplay")},
            {"break_stage",readField<int>(info,"CharInfo","breakStage","Beyond.Gameplay")},
            {"potential",readField<std::uint32_t>(info,"CharInfo","potentialLevel","Beyond.Gameplay")}};
        return validCharacterGrowth(result,modelPath_)?result:nlohmann::json();
    }
    nlohmann::json localGrowth() {
        auto* root=api.call(gm("Entity","get_rootCom","","Beyond.Gameplay.Core.RootComponent"),character);
        if(!root||classOf(root)!="Beyond.Gameplay.Core.CharacterRootComponent")return nullptr;
        return infoGrowth(api.call(gm("CharacterRootComponent","get_replicatedCharInfo","","Beyond.Gameplay.CharInfo"),root),localModel);
    }
    int itemNumber(const std::string& id) {
        constexpr const char* common="Common.Beyond.dll",*cfg="Beyond.Cfg";
        auto* table=api.call(m("Tables","get_strIdNumTable","","Beyond.Cfg.Tables.StrIdNumTable",common,cfg));
        eflink::unity::Api::TemporaryRoot tableRoot(runtime,table);
        auto* row=collection(table,"get_Item",{api.string("item_id")});eflink::unity::Api::TemporaryRoot rowRoot(runtime,row);
        auto* map=api.call(m("StrIdNumData","get_dic","","Beyond.SparkBuffer.Runtime.Wrapper.S2IntMap",common,cfg),runtime->object_unbox(runtime->context,row));
        eflink::unity::Api::TemporaryRoot mapRoot(runtime,map);
        const int result=api.value<int>(m("S2IntMap","get_Item","System.String","System.Int32","Unsafe.Beyond.dll","Beyond.SparkBuffer.Runtime.Wrapper"),
            runtime->object_unbox(runtime->context,map),{api.string(id)});
        if(result<=0)throw std::runtime_error("武器模板没有原生编号");return result;
    }
    std::string itemName(int id) {
        constexpr const char* common="Common.Beyond.dll",*cfg="Beyond.Cfg";
        auto* table=api.call(m("Tables","get_numIdStrTable","","Beyond.Cfg.Tables.NumIdStrTable",common,cfg));
        eflink::unity::Api::TemporaryRoot tableRoot(runtime,table);
        auto* row=collection(table,"get_Item",{api.string("item_id")});eflink::unity::Api::TemporaryRoot rowRoot(runtime,row);
        auto* map=api.call(m("NumIdStrData","get_dic","","Beyond.SparkBuffer.Runtime.Wrapper.I2StringMap",common,cfg),runtime->object_unbox(runtime->context,row));
        eflink::unity::Api::TemporaryRoot mapRoot(runtime,map);
        auto result=text(api.call(m("I2StringMap","get_Item","System.Int32","System.String","Unsafe.Beyond.dll","Beyond.SparkBuffer.Runtime.Wrapper"),
            runtime->object_unbox(runtime->context,map),{&id}));
        if(result.empty()||result.size()>120||itemNumber(result)!=id)throw std::runtime_error("武器模板编号无法往返校验");return result;
    }
    void applyActorAppearance(Actor& actor,const nlohmann::json& appearance) {
        if(appearance==actor.appearance)return;
        traceNative("actor.weapon.begin");
        if(!nativeWeaponLookupReady)throw std::runtime_error("原生房间武器查询适配未就绪");
        if(!currentActor(actor)||(!appearance.is_null()&&!validCharacterAppearance(appearance,actor.model)))throw std::runtime_error("远端武器资料身份无效");
        const int number=appearance.is_null()?0:appearance.at("weapon_template").get<int>();
        void* weapon=nullptr;std::unique_ptr<eflink::unity::Api::TemporaryRoot> weaponRoot;
        if(number) {
            const auto name=itemName(number);
            constexpr const char* common="Common.Beyond.dll",*cfg="Beyond.Cfg";
            auto* weapons=api.call(m("Tables","get_weaponBasicTable","","Beyond.Cfg.Tables.WeaponBasicTable",common,cfg));
            eflink::unity::Api::TemporaryRoot weaponsRoot(runtime,weapons);
            auto* weaponConfig=collection(weapons,"get_Item",{api.string(name)});eflink::unity::Api::TemporaryRoot weaponConfigRoot(runtime,weaponConfig);
            auto* characters=api.call(m("Tables","get_characterTable","","Beyond.Cfg.Tables.CharacterTable",common,cfg));
            eflink::unity::Api::TemporaryRoot charactersRoot(runtime,characters);
            auto* characterConfig=collection(characters,"get_Item",{api.string(actor.templateId)});eflink::unity::Api::TemporaryRoot characterConfigRoot(runtime,characterConfig);
            const auto weaponType=api.value<int>(m("WeaponBasicData","get_weaponType","","Beyond.GEnums.WeaponType",common,cfg),runtime->object_unbox(runtime->context,weaponConfig));
            const auto characterType=api.value<int>(m("CharacterData","get_weaponType","","Beyond.GEnums.WeaponType",common,cfg),runtime->object_unbox(runtime->context,characterConfig));
            if(weaponType!=characterType)throw std::runtime_error("武器类型与远端干员不兼容");
            weapon=api.call(gm("InventorySystem","CreateInitialGachaPoolWeaponInst","System.String","Beyond.Gameplay.InventorySystem.WeaponInstData","Beyond.Gameplay"),nullptr,{api.string(name)});
            if(!weapon)throw std::runtime_error("原生武器资料创建失败");
            weaponRoot=std::make_unique<eflink::unity::Api::TemporaryRoot>(runtime,weapon);
            field(weapon,"InventorySystem/ItemInstData","instId",actor.id,"Beyond.Gameplay");
            field(weapon,"InventorySystem/WeaponInstData","equippedCharServerId",actor.id,"Beyond.Gameplay");
        }
        // Only the independent ClientCharInfo owned by this room actor is changed.
        // It is never inserted into InventorySystem or the account's CharBag.
        auto* previous=readField<void*>(actor.info,"ClientCharInfo","m_weaponData","Beyond.Gameplay");
        eflink::unity::Api::TemporaryRoot previousRoot(runtime,previous);
        const auto previousId=readField<std::uint64_t>(actor.info,"CharInfo","weaponInstId","Beyond.Gameplay");
        auto* component=api.call(gm("Entity","get_weaponCom","","Beyond.Gameplay.View.WeaponComponent"),actor.entity);
        if(!component)throw std::runtime_error("原生武器组件尚未就绪");
        try {
            field(actor.info,"ClientCharInfo","m_weaponData",weapon,"Beyond.Gameplay");
            const auto weaponId=number?actor.id:std::uint64_t(0);field(actor.info,"CharInfo","weaponInstId",weaponId,"Beyond.Gameplay");
            if(!number) {
                // A missing instance would make native loading fall back to a
                // preset/default weapon. Unequip/unknown must release that view.
                bool immediate=true;api.call(gm("WeaponComponent","UnloadWeapons","System.Boolean","System.Void","Beyond.Gameplay.View"),component,{&immediate});
            } else {
            auto* bundles=readField<void*>(component,"WeaponComponent","m_weaponBundles","Beyond.Gameplay.View");
            auto* count= bundles?collection(bundles,"get_Count"):nullptr;
            const int modes=count?*static_cast<int*>(runtime->object_unbox(runtime->context,count)):0;
            if(!modes)api.call(gm("WeaponComponent","_InitWeapon","","System.Void","Beyond.Gameplay.View"),component);
            else {bool force=true;api.call(gm("WeaponComponent","ReloadWeapon","System.Boolean","System.Void","Beyond.Gameplay.View"),component,{&force});}
            }
        } catch(...) {
            field(actor.info,"ClientCharInfo","m_weaponData",previous,"Beyond.Gameplay");field(actor.info,"CharInfo","weaponInstId",previousId,"Beyond.Gameplay");
            throw;
        }
        actor.appearance=appearance;actor.weaponError.clear();++actor.weaponReloads;
        actor.weaponState=-1;
    }
    nlohmann::json localAppearance() {
        if(!character)return nullptr;
        if(lastAppearanceSample&&appearanceModel==localModel&&nowMs()-lastAppearanceSample<1000)return localAppearanceInfo;
        lastAppearanceSample=nowMs();appearanceModel=localModel;localAppearanceInfo=nullptr;appearanceError.clear();
        try {
            auto* root=api.call(gm("Entity","get_rootCom","","Beyond.Gameplay.Core.RootComponent"),character);
            if(!root||classOf(root)!="Beyond.Gameplay.Core.CharacterRootComponent")return nullptr;
            auto* info=api.call(gm("CharacterRootComponent","get_replicatedCharInfo","","Beyond.Gameplay.CharInfo"),root);
            if(info) {
                const auto characterId=readField<std::uint64_t>(info,"CharInfo","instId","Beyond.Gameplay");
                localAppearanceInfo=officialAppearance.profile(characterId,localModel);
                // The active native character is the final source of truth,
                // including late loading and changes made outside bag packets.
                {
                    const auto expected=readField<std::uint64_t>(info,"CharInfo","weaponInstId","Beyond.Gameplay");
                    int number=0;
                    if(expected) {
                        auto* weapon=collection(info,"GetWeaponInstData");
                        eflink::unity::Api::TemporaryRoot weaponRoot(runtime,weapon);
                        if(!weapon||readField<std::uint64_t>(weapon,"InventorySystem/ItemInstData","instId","Beyond.Gameplay")!=expected)
                            throw std::runtime_error("原生角色的武器实例未就绪");
                        number=itemNumber(text(readField<void*>(weapon,"InventorySystem/ItemInstData","templateId","Beyond.Gameplay")));
                    }
                    auto sampled=nlohmann::json{{"template",text(readField<void*>(info,"CharInfo","templateId","Beyond.Gameplay"))},{"weapon_template",number}};
                    if(validCharacterAppearance(sampled,localModel))localAppearanceInfo=std::move(sampled);
                }
            }
        }catch(const std::exception& e) {localAppearanceInfo=nullptr;appearanceError=e.what();}
        return localAppearanceInfo;
    }
    void destroyActor(Actor& actor) {
        if(session)session->unbindPlayer(actor.peer);
        if(actor.entity) {
            try {if(currentActor(actor))api.call(gm("Entity","Release"),actor.entity);}catch(...) {}
        }
        release(actor.entityRoot);release(actor.infoRoot);
        actor.entity=actor.info=nullptr;actor.registered=actor.started=actor.bound=false;
        actor.movement=nullptr;
        actor.identity={};
        actor.inited=false;actor.waiting=nlohmann::json::array();
        actor.catalog.clear();actor.skillCount=0;
    }
    void dropActors() {for(auto& [peer,actor]:actors)destroyActor(actor);actors.clear();actorTravelGrants.clear();}
    std::map<std::string,Realm::SkillRule> skillCatalog(void* ability) {
        std::map<std::string,Realm::SkillRule> catalog;
        auto* list=api.call(gm("AbilitySystem","get_skills","","System.Collections.Generic.List<Beyond.Gameplay.Core.Skill>"),ability);
        if(!list)return catalog;
        eflink::unity::Api::TemporaryRoot listRoot(runtime,list);
        auto* countBox=collection(list,"get_Count");if(!countBox)return catalog;
        int count=0;std::memcpy(&count,runtime->object_unbox(runtime->context,countBox),sizeof count);
        if(count<0||count>64)return catalog;
        for(int i=0;i<count;++i) {
            auto* skill=collection(list,"get_Item",{&i});if(!skill)continue;
            auto* data=api.call(gm("Skill","get_data","","Beyond.Gameplay.Core.SkillData"),skill);
            if(!data)continue;
            auto* castData=readField<void*>(data,"SkillData","castData");if(!castData)continue;
            const auto id=text(api.call(gm("Skill","get_skillId","","System.String"),skill));
            const int check=readField<int>(castData,"CastData","checkCastDistanceType");
            float distance=check==enumValue<int>("CheckCastDistanceType","NotCheck","Beyond.Gameplay")?120.f:
                api.value<float>(gm("Skill","get_castDistance","","System.Single"),skill);
            float cooldown=api.value<float>(gm("Skill","get_cooldown","","System.Single"),skill);
            if(id.empty()||!std::isfinite(distance)||!std::isfinite(cooldown)||cooldown<0||cooldown>3600)continue;
            // Native targeting remains the final range/cost validator. A skill with
            // unlimited cast distance still has the transport's 120 m safety bound.
            double range=distance<=0?120.:(std::min)(120.,double(distance));
            catalog.emplace(id,Realm::SkillRule{std::uint32_t(std::ceil(cooldown*1000)),range});
        }
        return catalog;
    }
    bool anySkillCasting(void* ability) {
        if(api.value<bool>(gm("AbilitySystem","get_inSkill","","System.Boolean"),ability))return true;
        auto* list=api.call(gm("AbilitySystem","get_skills","","System.Collections.Generic.List<Beyond.Gameplay.Core.Skill>"),ability);
        if(!list)return false;
        eflink::unity::Api::TemporaryRoot listRoot(runtime,list);auto* countBox=collection(list,"get_Count");int count=0;
        if(countBox)std::memcpy(&count,runtime->object_unbox(runtime->context,countBox),sizeof count);
        if(count<0||count>64)throw std::runtime_error("原生施法状态列表长度无效");
        for(int i=0;i<count;++i)if(auto* skill=collection(list,"get_Item",{&i})) {
            // Passive/buff-only skills have no timeline processor. The game's
            // get_isCasting accessor assumes both objects exist and throws for
            // those entries; inspect the metadata-resolved references first.
            auto* action=readField<void*>(skill,"Skill","m_ability");
            if(!action||!readField<void*>(action,"Ability","m_timelineActionProcessor"))continue;
            if(api.value<bool>(gm("Skill","get_isCasting","","System.Boolean"),skill))return true;
        }
        return false;
    }
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    nlohmann::json movementState(void* entity) {
        auto* movement=api.call(gm("Entity","get_movementComponent","","Beyond.Gameplay.Core.MovementComponent"),entity);
        if(!movement)return {{"available",false}};
        auto* controller=api.call(gm("Entity","get_charCtrl","","Beyond.Gameplay.Core.CharacterController"),entity);
        auto* ai=api.call(gm("Entity","get_charAICom","","Beyond.Gameplay.AI.CharacterAIComponent"),entity);
        auto velocity=api.value<V3>(gm("MovementComponent","get_velocity","","UnityEngine.Vector3"),movement);
        auto* capsule=readField<void*>(movement,"MovementComponent","m_capsule");
        auto state=nlohmann::json{{"available",true},{"entity_disabled",api.value<bool>(gm("Entity","get_isDisabled","","System.Boolean"),entity)},
            {"enabled",api.value<bool>(gm("MovementComponent","get_enabled","","System.Boolean"),movement)},
            {"enabled_by_camera",api.value<bool>(gm("MovementComponent","get_enabledByCamera","","System.Boolean"),movement)},
            {"enabled_by_factory_camera",api.value<bool>(gm("MovementComponent","get_enabledByFactoryCamera","","System.Boolean"),movement)},
            {"pipeline_available",readField<void*>(movement,"MovementComponent","m_pipeline")!=nullptr},{"capsule_alive",capsule&&alive(capsule)},
            {"movement_ticking",api.value<bool>(gm("TickComponent","IsTicking","","System.Boolean"),movement)},
            {"controller_ticking",controller&&api.value<bool>(gm("TickComponent","IsTicking","","System.Boolean"),controller)},
            {"ai_ticking",ai&&api.value<bool>(gm("TickComponent","IsTicking","","System.Boolean"),ai)},
            {"fixed_tick_index",readField<int>(movement,"MovementComponent","m_tickIndex")},
            {"last_fixed_tick_time",readField<double>(movement,"MovementComponent","m_lastFixedTickTime")},
            {"block_grounded",api.value<bool>(gm("MovementComponent","get_blockGroundedMove","","System.Boolean"),movement)},
            {"active_move",api.value<bool>(gm("MovementComponent","get_isActiveMove","","System.Boolean"),movement)},
            {"ground_speed",api.value<float>(gm("MovementComponent","get_realGroundSpeed","","System.Single"),movement)},
            {"velocity",{velocity.x,velocity.y,velocity.z}}};
        auto* group=api.call(gm("Entity","get_tickOwnerInfoGroup","","Beyond.TickOwnerInfoGroup"),entity);
        auto* owner=api.call(gm("TickComponent","GetTickOwnerInfo","","Beyond.TickOwnerInfo"),movement);
        auto* function=api.call(gm("TickComponent","get_fixedTickFunction","","Beyond.TickFunction"),movement);
        const char* common="Common.Beyond.dll";const char* space="Beyond";
        state["group_available"]=group!=nullptr;state["owner_available"]=owner!=nullptr;state["fixed_function_available"]=function!=nullptr;
        if(group)state["tick_group"]={{"registered",readField<bool>(group,"TickOwnerInfoGroup","m_isRegistered",space,common)},
            {"owned_by_entity",api.call(m("TickOwnerInfoGroup","GetOwner","","System.Object",common,space),group)==entity},
            {"time_scale",api.value<float>(m("TickOwnerInfoGroup","GetTimeScale","","System.Single",common,space),group)},
            {"skip_frame",api.value<bool>(m("TickOwnerInfoGroup","GetSkipTickThisFrame","","System.Boolean",common,space),group)}};
        if(owner)state["tick_owner"]={{"registered",api.value<bool>(m("TickOwnerInfo","IsRegistered","","System.Boolean",common,space),owner)},
            {"constructed",api.value<bool>(m("TickOwnerInfo","IsConstructed","","System.Boolean",common,space),owner)},
            {"time_scale",api.value<float>(m("TickOwnerInfo","GetTimeScale","","System.Single",common,space),owner)}};
        if(function)state["fixed_function"]={{"inited",api.value<bool>(m("TickFunction","IsInited","","System.Boolean",common,space),function)},
            {"released",api.value<bool>(m("TickFunction","IsReleased","","System.Boolean",common,space),function)},
            {"should_tick",api.value<bool>(m("TickFunction","ShouldTick","","System.Boolean",common,space),function)},
            {"last_timestamp",readField<double>(function,"TickFunction","lastTimeStamp",space,common)},
            {"last_frame",readField<int>(function,"TickFunction","lastFrameCount",space,common)}};
        return state;
    }
#endif
    void createActor(Actor& actor,V3 position,int levelNumber) {
        traceNative("actor.create.begin");
        auto templateId=operatorTemplate(actor.model);if(templateId.empty())throw std::runtime_error("原生玩家模板标识不可用");
        actor.templateId=templateId;
        // Independent client character growth/gear data. It is never inserted into
        // the account's CharBag/Squad, and cannot overwrite an account operator.
        actor.info=api.call(gm("ClientCharInfo","CreateInitialGachaPoolCharInfo","System.String","Beyond.Gameplay.ClientCharInfo","Beyond.Gameplay"),nullptr,{api.string(templateId)});
        if(!actor.info)throw std::runtime_error("原生玩家初始属性创建失败");
        actor.infoRoot=rootOf(actor.info);field(actor.info,"CharInfo","instId",actor.id,"Beyond.Gameplay");
        traceNative("actor.create.info_ready");
        if(!actor.growth.is_null()) {
            if(!validCharacterGrowth(actor.growth,actor.model))throw std::runtime_error("房间玩家成长资料与原生角色不匹配");
            constexpr const char* common="Common.Beyond.dll",*cfg="Beyond.Cfg";
            auto* limits=api.call(m("Tables","get_characterConst","","Beyond.Cfg.Tables.CharacterConst",common,cfg));
            eflink::unity::Api::TemporaryRoot limitRoot(runtime,limits);
            if(!limits)throw std::runtime_error("原生干员成长配置不可用");
            int level=actor.growth.at("level").get<int>(),stage=actor.growth.at("break_stage").get<int>();
            const auto potential=actor.growth.at("potential").get<std::uint32_t>();
            if(level>api.value<int>(m("Tables/CharacterConst","get_maxLevel","","System.Int32",common,cfg),limits)||
               stage>api.value<int>(m("Tables/CharacterConst","get_maxBreak","","System.Int32",common,cfg),limits)||
               potential>std::uint32_t(api.value<int>(m("Tables/CharacterConst","get_maxPotential","","System.Int32",common,cfg),limits)))
                throw std::runtime_error("房间玩家成长资料超过当前原生配置上限");
            auto* stages=api.call(m("Tables","get_charBreakStageTable","","Beyond.Cfg.Tables.CharBreakStageTable",common,cfg));
            eflink::unity::Api::TemporaryRoot stagesRoot(runtime,stages);
            auto* contains=stages?collection(stages,"ContainsKey",{&stage}):nullptr;
            auto* containsRaw=contains?runtime->object_unbox(runtime->context,contains):nullptr;
            if(!containsRaw||!(*static_cast<bool*>(containsRaw)))throw std::runtime_error("原生干员突破阶段不存在");
            auto* stageData=collection(stages,"get_Item",{&stage});
            eflink::unity::Api::TemporaryRoot stageRoot(runtime,stageData);
            auto* stageRaw=stageData?runtime->object_unbox(runtime->context,stageData):nullptr;
            if(!stageRaw||level<api.value<int>(m("CharBreakStageData","get_minCharLevel","","System.Int32",common,cfg),stageRaw)||
               level>api.value<int>(m("CharBreakStageData","get_maxCharLevel","","System.Int32",common,cfg),stageRaw))
                throw std::runtime_error("干员等级与原生突破阶段配置不匹配");
            field(actor.info,"CharInfo","level",level,"Beyond.Gameplay");field(actor.info,"CharInfo","breakStage",stage,"Beyond.Gameplay");
            field(actor.info,"CharInfo","potentialLevel",potential,"Beyond.Gameplay");
            // Factory presets already contain cached attributes. Recompute through
            // native configuration after applying growth; never copy claimed HP.
            api.call(gm("ClientCharInfo","InitClientCardSkill","","System.Void","Beyond.Gameplay"),actor.info);
            api.call(gm("CharInfo","InitDeckData","","System.Void","Beyond.Gameplay"),actor.info);
            bool force=true;api.call(gm("CharInfo","InitSkillData","System.Boolean","System.Void","Beyond.Gameplay"),actor.info,{&force});
        }
        auto* templateInfo=runtime->object_new(runtime->context,api.klass("CharacterInfo","Gameplay.Beyond.dll","Beyond.Gameplay.Core").class_info);
        eflink::unity::Api::TemporaryRoot templateRoot(runtime,templateInfo);
        api.call(gm("CharacterInfo",".ctor"),templateInfo);api.call(gm("BaseEntityData","OnAllocate"),templateInfo);
        api.call(gm("BaseEntityData","SetDataBitForClientEntityFromTemplate"),templateInfo);
        if(!api.value<bool>(gm("BaseEntityData","SetupTemplate","System.String","System.Boolean"),templateInfo,{api.string(templateId)}))throw std::runtime_error("原生玩家模板不存在: "+templateId);
        auto* entityTemplate=api.call(gm("BaseEntityData","get_templateData","","Beyond.Gameplay.EntityTemplateData"),templateInfo);
        auto* worldInfo=staticObject("worldInfo");auto* container=staticObject("objectContainer");
        if(!worldInfo||!container||!entityTemplate)throw std::runtime_error("原生实体容器未就绪");
        auto* allocator=api.call(gm("WorldInfo","get_entityAllocator","","Beyond.Gameplay.Core.EntityAllocator"),worldInfo);
        auto* parameter=runtime->object_new(runtime->context,api.klass("EntityAllocator/AllocObjParam","Gameplay.Beyond.dll","Beyond.Gameplay.Core").class_info);
        eflink::unity::Api::TemporaryRoot paramRoot(runtime,parameter);auto* raw=runtime->object_unbox(runtime->context,parameter);V3 rotation{};
        if(!raw)throw std::runtime_error("原生角色分配参数 ABI 不可用");
        api.call(gm("EntityAllocator/AllocObjParam",".ctor","UnityEngine.Vector3|UnityEngine.Vector3"),raw,{&position,&rotation});
        traceNative("actor.create.allocate");
        actor.entity=api.call(gm("EntityAllocator","AllocateObject","Beyond.Gameplay.EntityTemplateData|System.UInt64|Beyond.Gameplay.Core.EntityAllocator.AllocObjParam|Beyond.Gameplay.LevelEntityData","Beyond.Gameplay.Core.Entity"),allocator,{entityTemplate,&actor.id,raw,nullptr});
        if(!actor.entity)throw std::runtime_error("原生角色分配失败");
        actor.entityRoot=rootOf(actor.entity);
        actor.identity=identityOf(actor.entity);
        auto* root=api.call(gm("Entity","get_rootCom","","Beyond.Gameplay.Core.RootComponent"),actor.entity);
        if(!root||classOf(root)!="Beyond.Gameplay.Core.CharacterRootComponent")throw std::runtime_error("原生角色根组件类型不匹配");
        traceNative("actor.create.server_data");
        api.call(gm("CharacterRootComponent","InitServerData","Beyond.Gameplay.CharInfo"),root,{actor.info});
        bool no=false;api.call(gm("CharacterRootComponent","set_isServer","System.Boolean"),root,{&no});
        api.call(gm("Entity","set_levelId","System.Int32"),actor.entity,{&levelNumber});
        auto generation=std::uint32_t(actor.epoch);api.call(gm("Entity","AssignServerGeneration","System.UInt32"),actor.entity,{&generation});
        actor.identity=identityOf(actor.entity);
        api.call(gm("Entity","set_serverCreated","System.Boolean"),actor.entity,{&no});
        api.call(gm("ObjectContainer","Register","Beyond.Gameplay.Core.Entity|System.UInt64"),container,{actor.entity,&actor.id});actor.registered=true;
        actor.identity=identityOf(actor.entity);
        bool async=true;api.call(gm("Entity","Init","System.Boolean"),actor.entity,{&async});
        api.call(gm("Entity","TryStart"),actor.entity);
        traceNative("actor.create.started");
    }
    void updateActors(Net& net,Session& link) {
        const auto view=presentationView(net.snapshot(),link);
        if((view.role!=Role::Host&&view.role!=Role::Client)||!link.ready()||!character||!link.localSceneOperational()) {if(!actors.empty())dropActors();return;}
        const auto& snapshot=link.snapshot();
        if(snapshot.value("map",std::string{})!=mapId||snapshot.value("level",std::string{})!=levelId) {dropActors();return;}
        std::set<std::uint16_t> present;
        for(auto& [peer,grant]:link.takeActorTeleports())actorTravelGrants[peer]=std::move(grant);
        for(auto it=actorTravelGrants.begin();it!=actorTravelGrants.end();) {
            if(it->second.value("epoch",std::uint64_t{})!=link.generation()||nowMs()-it->second.value("issued",std::uint64_t{})>5000)
                it=actorTravelGrants.erase(it);else ++it;
        }
        for(const auto& peer:view.peers) {
            if(peer.local||!peer.hasInfo||peer.info.mapId!=mapId||peer.info.levelId!=levelId||peer.states.empty()||
               !(peer.states.back().state.flags&proto::StateValid)||nowMs()-peer.states.back().receivedMs>3000)continue;
            const bool extra=peer.id>=0x8000;
            const auto id=extra?(0xEF20000000000000ull|(std::uint64_t(view.roomId)<<16)|peer.id):link.playerEntity(peer.id);if(!id)continue;
            present.insert(peer.id);auto& actor=actors[peer.id];
            auto growth=extra?extraMembers.at(peer.id).at("growth"):link.requestedGrowth(peer.id);
            if(actor.id!=id||actor.model!=peer.info.modelPath||actor.epoch!=link.generation()||actor.growth!=growth||(extra&&actor.presentationLife!=extraMembers.at(peer.id).at("life").get<std::uint64_t>())) {
                destroyActor(actor);actor=Actor{};actor.id=id;actor.peer=peer.id;actor.epoch=link.generation();actor.model=peer.info.modelPath;
                actor.growth=std::move(growth);if(extra)actor.presentationLife=extraMembers.at(peer.id).at("life").get<std::uint64_t>();
            }
            auto& pose=peer.states.back().state;V3 position{pose.position[0],pose.position[1],pose.position[2]};
            const auto authoritative=link.nativeState(peer.id);
            if(view.role==Role::Client&&authoritative)position={float(authoritative->position[0]),float(authoritative->position[1]),float(authoritative->position[2])};
            try {
                if(!actor.entity) {
                    if(nowMs()<actor.retry)continue;
                    actor.retry=nowMs()+15000;
                    auto spawnPosition=position;
                    if(view.role==Role::Host) {const auto& p=snapshot.at("position");spawnPosition={p[0].get<float>(),p[1].get<float>(),p[2].get<float>()};}
                    createActor(actor,spawnPosition,snapshot.value("level_num",0));
                }
                if(!currentActor(actor))throw std::runtime_error("原生角色已释放或分配给新的实体");
                actor.inited=api.value<bool>(gm("Entity","get_hasInited","","System.Boolean"),actor.entity);
                if(actor.inited&&!actor.started)api.call(gm("Entity","TryStart"),actor.entity);
                actor.started=api.value<bool>(gm("Entity","get_hasStarted","","System.Boolean"),actor.entity);
                if(!actor.started) {
                    actor.error="等待原生角色组件启动";actor.waiting=nlohmann::json::array();
                    auto* list=api.call(gm("Entity","DebugOnlyGetUnInitedComponents","","System.Collections.Generic.List<Beyond.Gameplay.Core.BaseComponent>"),actor.entity);
                    if(list) {
                        eflink::unity::Api::TemporaryRoot root(runtime,list);auto* countBox=collection(list,"get_Count");int count=0;
                        if(countBox)std::memcpy(&count,runtime->object_unbox(runtime->context,countBox),sizeof count);
                        for(int i=0;i<count&&i<32;++i) {auto* component=collection(list,"get_Item",{&i});if(component)actor.waiting.push_back(classOf(component));}
                    }
                    continue;
                }
                actor.waiting=nlohmann::json::array();
                try {
                    if(nowMs()>=actor.weaponRetry)applyActorAppearance(actor,extra?extraMembers.at(peer.id).at("appearance"):link.requestedAppearance(peer.id));
                    if(auto* weapon=api.call(gm("Entity","get_weaponCom","","Beyond.Gameplay.View.WeaponComponent"),actor.entity))
                        actor.weaponLoaded=readField<bool>(weapon,"WeaponComponent","m_weaponLoaded","Beyond.Gameplay.View");
                    if(!extra)link.recordAppearance(peer.id,actor.id,actor.epoch,actor.appearance,actor.weaponError.empty()&&actor.weaponReloads&&
                        !actor.appearance.is_null()&&actor.appearance.contains("weapon_template")&&(actor.weaponLoaded||actor.appearance.at("weapon_template")==0));
                    if(actor.weaponLoaded&&(pose.flags&proto::StateWeaponValid)&&proto::weaponState(pose.flags)<=5&&actor.weaponState!=int(proto::weaponState(pose.flags))) {
                        auto* weapon=api.call(gm("Entity","get_weaponCom","","Beyond.Gameplay.View.WeaponComponent"),actor.entity);
                        int state=int(proto::weaponState(pose.flags)),priority=enumValue<int>("WeaponComponent/WeaponStatePriority","Perform","Beyond.Gameplay.View");bool immediate=true;
                        api.call(gm("WeaponComponent","_ForceSetStateAll","Beyond.Gameplay.View.WeaponComponent.WeaponState|Beyond.Gameplay.View.WeaponComponent.WeaponStatePriority|System.Boolean","System.Int32","Beyond.Gameplay.View"),weapon,{&state,&priority,&immediate});
                        actor.weaponState=state;
                    }
                }catch(const std::exception& e) {actor.weaponError=e.what();actor.weaponRetry=nowMs()+2000;}
                auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),actor.entity);
                auto* movement=api.call(gm("Entity","get_movementComponent","","Beyond.Gameplay.Core.MovementComponent"),actor.entity);
                actor.movement=movement;
                if(!ability||!movement) {actor.error="原生角色缺少能力或移动组件";continue;}
                actor.hp=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability);
                actor.maxHp=api.value<double>(gm("AbilitySystem","get_maxHp","","System.Double"),ability);
                if(!std::isfinite(actor.hp)||!std::isfinite(actor.maxHp)||actor.maxHp<=0) {actor.error="原生角色属性未就绪";continue;}
                if(!actor.hpInitialized) {
                    bool skip=true;api.call(gm("AbilitySystem","SetHpDirectly","System.Double|System.Boolean"),ability,{&actor.maxHp,&skip});
                    actor.hp=actor.maxHp;actor.hpInitialized=true;
                }
                if(!actor.aiPaused) {
                    auto* ai=api.call(gm("Entity","get_charAICom","","Beyond.Gameplay.AI.CharacterAIComponent"),actor.entity);
                    if(ai)api.call(gm("TickComponent","PauseTick"),ai);actor.aiPaused=true;
                }
                // Normal attacks do not necessarily set AbilitySystem.inSkill.
                // Never teleport the caster back to a UDP pose during an actual
                // Skill cast, or overwrite its native root motion/bone animation.
                actor.casting=anySkillCasting(ability);
                actor.abilityTicking=api.value<bool>(gm("TickComponent","IsTicking","","System.Boolean"),ability);
#ifdef EFLINK_NATIVE_DIAGNOSTICS
                if(nowMs()-actor.lastDiagnostics>=1000) {actor.lastDiagnostics=nowMs();actor.movementDiagnostics=movementState(actor.entity);}
#endif
                // Each player owns their own body, as in an official co-op world:
                // the actor follows that player's interpolated pose every frame
                // (updateRemotes), so its animation and position match exactly.
                // Steering it with CharacterController.MoveTo overshot and hunted
                // around the target, making a standing guest jitter on the host.
                // A travel grant needs no extra write; the guest's own pose arrives.
                if(actorTravelGrants.erase(peer.id))++actor.teleports;
                // Verify native lookup ownership instead of accepting a visible mesh.
                auto* registered=api.call(gm("ObjectContainer","GetEntityById","System.UInt64","Beyond.Gameplay.Core.Entity"),staticObject("objectContainer"),{&actor.id});
                if(registered!=actor.entity) {actor.error="原生角色未注册到实体容器";continue;}
                if(!extra&&actor.catalog.empty())actor.catalog=skillCatalog(ability);
                actor.skillCount=actor.catalog.size();
                if(!extra&&view.role==Role::Host&&!actor.bound&&!actor.catalog.empty())actor.bound=link.bindPlayer(peer.id,actor.model,actor.catalog);
                if(!extra&&view.role==Role::Host&&actor.bound) {
                    if(auto confirmed=infoGrowth(actor.info,actor.model);!confirmed.is_null())link.recordGrowth(peer.id,actor.id,actor.epoch,confirmed);
                    auto actual=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),actor.entity);
                    link.observePlayer(peer.id,actor.id,actor.epoch,{{actual.x,actual.y,actual.z},actor.hp,actor.maxHp,api.value<bool>(entityAlive,actor.entity)});
                }
                actor.error.clear();
            } catch(const std::exception& e) {auto error=std::string(e.what());destroyActor(actor);actor.error=error;actor.retry=nowMs()+15000;}
        }
        for(auto it=actors.begin();it!=actors.end();) {if(!present.contains(it->first)){destroyActor(it->second);it=actors.erase(it);}else ++it;}
        if(view.role==Role::Host) {
            auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),character);
            if(ability) {
                link.bindPlayer(view.localId,localModel,skillCatalog(ability));
                if(auto growth=localGrowth();!growth.is_null())link.recordGrowth(view.localId,link.playerEntity(view.localId),link.generation(),growth);
                auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
                const auto hp=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability),max=api.value<double>(gm("AbilitySystem","get_maxHp","","System.Double"),ability);
                link.observePlayer(view.localId,link.playerEntity(view.localId),link.generation(),{{p.x,p.y,p.z},(std::max)(0.,hp),max,api.value<bool>(entityAlive,character)});
            }
        }
    }
    bool castNative(std::uint16_t peer,const std::string& key,const std::string& requested,bool diagnostic=false,bool moveOwned=false) {
        auto actor=actors.find(peer);auto enemy=enemies.find(key);
        if(actor==actors.end()||!actor->second.started||!actor->second.bound||!currentActor(actor->second)||
           enemy==enemies.end()||!currentEnemy(enemy->second))throw std::runtime_error("原生施法角色或目标未就绪");
        auto* caster=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),actor->second.entity);
        auto* target=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),enemy->second.entity);
        if(!caster||!target||!api.value<bool>(entityAlive,enemy->second.entity))throw std::runtime_error("原生施法目标无效");
        void* skill=nullptr;
        auto requestedId=requested;
        if(requestedId.empty()&&diagnostic) {
            auto* attacks=api.call(gm("AbilitySystem","get_curNormalAttackList","","System.Collections.Generic.List<System.String>"),caster);
            if(attacks) {
                eflink::unity::Api::TemporaryRoot attacksRoot(runtime,attacks);auto* countBox=collection(attacks,"get_Count");int count=0;
                if(countBox)std::memcpy(&count,runtime->object_unbox(runtime->context,countBox),sizeof count);
                if(count>0&&count<=64) {int first=0;requestedId=text(collection(attacks,"get_Item",{&first}));}
            }
        }
        if(!requestedId.empty())api.value<bool>(gm("AbilitySystem","TryGetSkill","System.String|Beyond.Gameplay.Core.Skill&","System.Boolean"),caster,{api.string(requestedId),&skill});
        if(!skill)throw std::runtime_error("原生技能不存在");
        const auto id=text(api.call(gm("Skill","get_skillId","","System.String"),skill));
        if(!skillCatalog(caster).contains(id))throw std::runtime_error("技能不在原生主动技能目录中");
        const auto before=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),target);
#ifdef EFLINK_NATIVE_DIAGNOSTICS
        if(diagnostic&&moveOwned) {
            // Development-only test positioning affects our owned remote actor,
            // never the host's operator or another module's entity.
            auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),enemy->second.entity);p.z-=2;
            auto* movement=api.call(gm("Entity","get_movementComponent","","Beyond.Gameplay.Core.MovementComponent"),actor->second.entity);
            bool warning=false;api.call(gm("MovementComponent","TeleportTo","UnityEngine.Vector3|System.Boolean"),movement,{&p,&warning});
        }
#else
        (void)moveOwned;
#endif
        // Let this actor's own native skill build an entity target. No client
        // target pointer, damage amount, or host-character substitution is used.
        float limit=120.f;auto* handle=api.call(gm("Skill","GetDefaultTarget","Beyond.Gameplay.Core.AbilitySystem|System.Single","Beyond.Gameplay.Core.TargetHandle"),skill,{target,&limit});
        if(!handle)throw std::runtime_error("原生技能没有有效目标");
        eflink::unity::Api::TemporaryRoot handleRoot(runtime,handle);
        auto* view=runtime->object_new(runtime->context,api.klass("TargetHandleView","Gameplay.Beyond.dll","Beyond.Gameplay.Core").class_info);
        eflink::unity::Api::TemporaryRoot viewRoot(runtime,view);auto* viewRaw=runtime->object_unbox(runtime->context,view);
        api.call(gm("TargetHandleView",".ctor","Beyond.Gameplay.Core.TargetHandle"),viewRaw,{handle});
        if(!api.value<bool>(gm("TargetHandleView","Contains","Beyond.Gameplay.Core.AbilitySystem","System.Boolean"),viewRaw,{target}))
            throw std::runtime_error("原生技能目标选择未包含房主实体");
        bool no=false;
        const bool can=api.value<bool>(gm("AbilitySystem","CanCastSkill","System.String|Beyond.Gameplay.Core.TargetHandleView|System.Boolean|System.Boolean|System.Boolean","System.Boolean"),caster,{api.string(id),viewRaw,&no,&no,&no});
        auto cast=gm("AbilitySystem","TryCastSkill","System.String|Beyond.Gameplay.Core.TargetHandleView|System.Action<Beyond.Gameplay.Core.Skill.FinishType,System.Int32>|System.Nullable<Beyond.Gameplay.Core.Skill.CastSkillOptions>|System.Action<Beyond.Gameplay.Core.Skill>|System.Action<Beyond.Gameplay.Core.Skill>|Beyond.Gameplay.Core.Skill.SkillCastInfo&|System.Boolean","System.Boolean");
        auto* options=api.parameterValue(cast,3);eflink::unity::Api::TemporaryRoot optionsRoot(runtime,options);
        auto* info=runtime->object_new(runtime->context,api.klass("Skill/SkillCastInfo","Gameplay.Beyond.dll","Beyond.Gameplay.Core").class_info);
        eflink::unity::Api::TemporaryRoot infoRoot(runtime,info);
        const bool accepted=can&&api.value<bool>(cast,caster,{api.string(id),viewRaw,nullptr,runtime->object_unbox(runtime->context,options),nullptr,nullptr,runtime->object_unbox(runtime->context,info),&no});
        if(accepted){++nativeCasts;actor->second.casting=true;}else ++nativeCastRejected;
        lastNativeCast={{"peer",peer},{"actor",actor->second.id},{"key",key},{"skill",id},{"diagnostic",diagnostic},
            {"native_can_cast",can},{"native_accepted",accepted},{"target_hp_before",before},{"issued",nowMs()}};
        return accepted;
    }
    void observeNativeCast() {
        if(lastNativeCast.empty())return;
        auto enemy=enemies.find(lastNativeCast.value("key",std::string{}));if(enemy==enemies.end()||!currentEnemy(enemy->second))return;
        auto* target=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),enemy->second.entity);
        if(target)lastNativeCast["target_hp_observed"]=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),target);
    }
    void teleportTo(V3 position) {
        if(!usable(character)||api.value<bool>(entityCinematic,character)) throw std::runtime_error("当前角色未就绪或正在过场");
        auto* movement=api.call(gm("Entity","get_movementComponent","","Beyond.Gameplay.Core.MovementComponent"),character);
        if(!movement) throw std::runtime_error("当前角色没有移动组件");
        bool warning=false;api.call(gm("MovementComponent","TeleportTo","UnityEngine.Vector3|System.Boolean"),movement,{&position,&warning});
    }
    void updateTravel() {
        if(!travel) return;
        auto* levelLoader=staticObject("levelLoader");
        if(nowMs()>travel->until) {travelState="failed";replicationError="地图加载或传送超时；未报告成功";travel.reset();return;}
        if(!levelLoader||!character) {travelState="waiting_scene";return;}
        auto* info=staticObject("worldInfo");
        if(!info) {travelState="waiting_scene";return;}
        auto* mode=api.call(gm("GameInstance","get_mode","","Beyond.Gameplay.Core.BaseGameMode","Beyond.Gameplay"));
        auto* instance=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
        auto* dungeon=api.call(gm("GameInstance","get_dungeonManager","","Beyond.Gameplay.Core.DungeonManager","Beyond.Gameplay"));
        auto* subgame=staticObject("subGameManager");
        const bool loading=api.value<bool>(gm("GameLevelLoader","get_isLoading","","System.Boolean"),levelLoader);
        const bool started=api.value<bool>(gm("WorldInfo","get_isStarted","","System.Boolean"),info);
        const bool delayed=instance&&readField<bool>(instance,"GameInstance","m_isModeDelayedToSwitch","Beyond.Gameplay");
        const bool inDungeon=dungeon&&api.value<bool>(gm("DungeonManager","get_inDungeon","","System.Boolean"),dungeon);
        const bool inSubgame=subgame&&api.value<bool>(gm("SubGameManager","get_inSubGame","","System.Boolean"),subgame);
        const auto modeId=mode?text(api.call(gm("BaseGameMode","get_modeId","","System.String"),mode)):std::string{};
        const bool mainScope=api.value<bool>(gm("GameInstance","get_isMainScope","","System.Boolean","Beyond.Gameplay"));
        travelLifecycle={{"world_started",started},{"mode_switch_delayed",delayed},{"mode_id",modeId},
            {"in_dungeon",inDungeon},{"in_subgame",inSubgame},{"main_scope",mainScope},{"loading",loading}};
        if(loading) {
            travel->observedLoading=true;travelState="loading";
            travelLifecycle["loading_ms"]=nowMs()-travel->startedAt;
            travelLifecycle["progress"]=api.value<float>(gm("GameLevelLoader","get_progress","","System.Single"),levelLoader);
            try {if(auto* step=readField<void*>(levelLoader,"GameLevelLoader","m_curStep"))travelLifecycle["step"]=classOf(step);}catch(...) {}
            // Record whether the game's pipeline actually shows its loading panel.
            try {
                auto* pipeline=readField<void*>(levelLoader,"GameLevelLoader","m_curLoadingPipeline");
                const auto type=pipeline?classOf(pipeline):std::string{};
                if(type.ends_with("LoadingPipeline")&&!type.ends_with("SeamlessLoadingPipeline")) {
                    travelLifecycle["panel_hidden"]=readField<bool>(pipeline,"GameLevelLoader/LoadingPipeline","m_noLoading");
                    travelLifecycle["short_teleport"]=readField<bool>(pipeline,"GameLevelLoader/LoadingPipeline","m_isShortTp");
                }
            }catch(const std::exception& e) {travelLifecycle["panel_error"]=e.what();}
            return;
        }
        // Official hub teleport in flight: wait for the target map, then walk the
        // rest within that map like any same-map move.
        if(travel->official) {
            if(mapId!=travel->map) {travelState="official_teleport";return;}
            travel->official=false;travel->started=false;travel->observedLoading=false;
        }
        {
            // Dungeons are entered and left by this account through the game's own
            // dungeon flow (CS_ENTER_DUNGEON / LeaveDungeon), never a local load.
            const auto current=inDungeon?text(api.call(gm("DungeonManager","get_curDungeonId","","System.String"),dungeon)):std::string{};
            if(travel->dungeonStage==1) {if(inDungeon) {travelState="leaving_dungeon";return;}travel->dungeonStage=0;travel->started=false;}
            if(travel->dungeonStage==2) {if(current!=travel->dungeon||!started) {travelState="entering_dungeon";return;}travel->dungeonStage=0;travel->started=false;travel->observedLoading=false;}
            if(inDungeon&&current!=travel->dungeon) {
                {OwnOfficial own(ownOfficialAction);api.call(gm("DungeonManager","LeaveDungeon"),dungeon);}
                travel->dungeonStage=1;travelState="leaving_dungeon";return;
            }
            if(!travel->dungeon.empty()&&!inDungeon) {
                if(!dungeon)throw std::runtime_error("副本管理器尚未就绪");
                auto* id=api.string(travel->dungeon);
                auto* instance_=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
                auto* player=instance_?api.call(gm("GameInstance","get_player","","Beyond.Gameplay.GamePlayer","Beyond.Gameplay"),instance_):nullptr;
                auto* bag=player?readField<void*>(player,"GamePlayer","charBag","Beyond.Gameplay"):nullptr;
                int team=bag?api.value<int>(gm("CharBagSystem","get_curTeamIndex","","System.Int32","Beyond.Gameplay"),bag):0;
                auto* preset=api.string("");
                OwnOfficial own(ownOfficialAction);
                const bool requested=api.value<bool>(gm("DungeonManager","TryReqEnterDungeon","System.String|System.String|System.Int32","System.Boolean"),dungeon,{id,preset,&team});
                if(!requested)throw std::runtime_error("游戏未允许进入副本 "+travel->dungeon+"（未解锁或条件不满足，见游戏内提示）");
                officialTeleportStatus={{"purpose","follow_dungeon"},{"dungeon",travel->dungeon},{"team",team},{"at",nowMs()}};
                travel->dungeonStage=2;travelState="entering_dungeon";return;
            }
        }
        const bool targetLoaded=mapId==travel->map&&levelId==travel->level;
        const bool overworld=(mainScope&&mode&&modeId.empty()&&!inDungeon&&!inSubgame&&
            mapId.rfind("map",0)==0&&travel->map.rfind("map",0)==0)||(inDungeon&&!travel->dungeon.empty());
        // Cross-level travel must not strand a private dungeon/subgame in an
        // overworld scene. Its full exit/room-rule adapter is still incomplete.
        // Preserve the original native scene instead of bypassing that lifecycle.
        if((!targetLoaded||travel->started)&&!overworld)
            throw std::runtime_error("副本或子玩法的联机切图生命周期尚未就绪；当前场景已保留，请先通过游戏退出副本");
        // The loader queues a pipeline; wait until it has actually begun before
        // treating "not loading" as finished.
        if(travel->started&&!travel->observedLoading&&nowMs()-travel->startedAt<3000) {travelState="loading_requested";return;}
        const bool sameMap=mapId==travel->map;
        if(sameMap&&!started&&travel->started) {
            // LoadAtPos can leave a loaded world awaiting StartWorld. Use the
            // idempotent native lifecycle, including delayed mode activation;
            // never write isStarted or clear arbitrary UI-forbid handles.
            bool inNewMap=true;
            api.call(gm("WorldInfo","StartWorld","System.Boolean"),info,{&inNewMap});
            travelState="starting_world";return;
        }
        if(!started||delayed) {travelState="waiting_world_start";return;}
        if(sameMap&&(travel->started||!overworld)) {
            teleportTo(travel->position);
            auto actual=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
            auto p=travel->position;float d=(actual.x-p.x)*(actual.x-p.x)+(actual.y-p.y)*(actual.y-p.y)+(actual.z-p.z)*(actual.z-p.z);
            if(d<9) {travelState="complete";++completedTeleports;travel.reset();release(nativeTravelParameterRoot);traceNative("scene.load_parameter.released");}
            else if(travel->started) {travel->started=false;travel->observedLoading=false;} // load once more, bounded below
            return;
        }
        // Entering or leaving a room is a world change: run it through the game's
        // own loading pipeline with the loading panel forced on, the same path as
        // an official teleport, instead of silently moving the character.
        if(mapId!=travel->map&&travel->dungeon.empty()) {
            // Another map: the account itself must get there through an official
            // teleport (hub on the target map nearest the destination).
            officialHubTeleport(travel->map,travel->position,travel->homeward?"return_home":"follow_host");
            travel->official=true;travel->started=true;travel->observedLoading=false;travel->startedAt=nowMs();travelState="official_teleport";
            return;
        }
        auto* parameter=runtime->object_new(runtime->context,api.klass("TeleportParam","Gameplay.Beyond.dll","Beyond.Gameplay").class_info);
        eflink::unity::Api::TemporaryRoot parameterRoot(runtime,parameter);
        field(parameter,"TeleportParam","uiType",enumValue<int>("TeleportUIType","WithLoading","Beyond.Gameplay"),"Beyond.Gameplay");
        auto* raw=runtime->object_unbox(runtime->context,parameter);if(!raw) throw std::runtime_error("地图传送参数 ABI 不可用");
        release(nativeTravelParameterRoot);nativeTravelParameterRoot=rootOf(parameter);
        if(!nativeTravelParameterRoot)throw std::runtime_error("地图传送参数生命周期保护失败");
        // Tear room objects down in the old, still valid scene. Do not wait for
        // the next frame to discover that the loader has already started.
        traceNative("scene.prepare_room_objects.begin");
        if(session)session->invalidateNativeScene();
        clearPlayerCastObservations();clear();dropActors();dropEnemies();
        lastAppliedEpoch=lastAppliedRevision=0;
        traceNative("scene.prepare_room_objects.end");
        travel->started=true;travel->observedLoading=false;travel->startedAt=nowMs();travelState="loading";
        if(mapId==travel->map) {
            if(++travel->inMapLoads>2)throw std::runtime_error("同图加载两次仍未到达目标位置，已停止传送");
            try {
                traceNative("scene.load_at_position.begin");
                api.call(gm("GameLevelLoader","LoadAtPosInCurrentMap","UnityEngine.Vector3|Beyond.Gameplay.TeleportParam&|System.Action|System.Action"),levelLoader,{&travel->position,raw,nullptr,nullptr});
                traceNative("scene.load_at_position.returned");
            }catch(const std::exception& e) {
                // Still arrive, without the panel, rather than strand the player.
                replicationError=std::string("原生读条传送不可用，已直接传送: ")+e.what();
                travel->observedLoading=true;
            }
            return;
        }
        // Resolve through native level-loader tables, using the host's validated numeric
        // level from its current world snapshot (cross-map target must be that world).
        int level=travel->levelNumber;if(level<=0) throw std::runtime_error("目标地图编号未就绪");
        int source=enumValue<int>("GameLevelLoader/LoadingSource","ClientTeleport");
        api.call(gm("GameLevelLoader","LoadAtPos","Beyond.Gameplay.Core.GameLevelLoader.LoadingSource|System.Int32|UnityEngine.Vector3|Beyond.Gameplay.TeleportParam&|System.Action|System.Action"),levelLoader,{&source,&level,&travel->position,raw,nullptr,nullptr});
    }
    // This account's teleportable factory hubs on a map (marks of GamePlayer.mapManager).
    std::vector<HubTarget> hubTargets(const std::string& mapName) {
        std::vector<HubTarget> out;
        auto* instance=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
        auto* player=instance?api.call(gm("GameInstance","get_player","","Beyond.Gameplay.GamePlayer","Beyond.Gameplay"),instance):nullptr;
        auto* map=player?readField<void*>(player,"GamePlayer","mapManager","Beyond.Gameplay"):nullptr;
        if(!map)throw std::runtime_error("地图管理器尚未就绪");
        auto* sequence=api.call(gm("MapManager","_EnumerateMarksInMap","System.String","System.Collections.Generic.IEnumerable<Beyond.Gameplay.MarkInstRuntimeData>","Beyond.Gameplay"),map,{api.string(mapName)});
        if(!sequence)return out;
        eflink::unity::Api::TemporaryRoot sequenceRoot(runtime,sequence);
        auto objectClass=symbol<const void*(*)(void*)>("il2cpp_object_get_class");
        auto methodsOf=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_methods");
        auto methodName=symbol<const char*(*)(const void*)>("il2cpp_method_get_name");
        auto paramCount=symbol<unsigned(*)(const void*)>("il2cpp_method_get_param_count");
        auto find=[&](void* object,const char* suffix)->const void* {
            void* it=nullptr;const void* found=nullptr;const auto n=std::strlen(suffix);
            while(const auto* m=methodsOf(objectClass(object),&it)) {
                const std::string name=methodName(m);
                if(paramCount(m)==0&&name.size()>=n&&name.compare(name.size()-n,n,suffix)==0) {found=m;if(name.find("Generic")!=std::string::npos)break;}
            }
            return found;};
        auto invoke=[&](const void* method,void* self)->void* {BE_ResolvedMethodV1 r{};r.method_info=method;return api.call(r,self);};
        const auto* getEnumerator=find(sequence,"GetEnumerator");if(!getEnumerator)return out;
        auto* enumerator=invoke(getEnumerator,sequence);if(!enumerator)return out;
        eflink::unity::Api::TemporaryRoot enumeratorRoot(runtime,enumerator);
        const auto* moveNext=find(enumerator,"MoveNext");const auto* current=find(enumerator,"get_Current");
        if(!moveNext||!current)throw std::runtime_error("地图标记枚举不可用");
        for(int guard=0;guard<4096;++guard) {
            auto* more=invoke(moveNext,enumerator);if(!more||!*static_cast<bool*>(runtime->object_unbox(runtime->context,more)))break;
            auto* mark=invoke(current,enumerator);
            if(!mark||classOf(mark)!="Beyond.Gameplay.FacMarkInstRuntimeData")continue;
            if(!readField<bool>(mark,"FacMarkInstRuntimeData","isTpRelated","Beyond.Gameplay"))continue;
            HubTarget hub;hub.node=readField<std::uint32_t>(mark,"FacMarkInstRuntimeData","nodeId","Beyond.Gameplay");if(!hub.node)continue;
            hub.level=text(readField<void*>(mark,"MarkInstRuntimeData","levelId","Beyond.Gameplay"));
            hub.inst=text(readField<void*>(mark,"MarkInstRuntimeData","instId","Beyond.Gameplay"));
            const auto* position=find(mark,"GetTeleportPosition");const auto* rotation=find(mark,"GetTeleportRotation");
            if(position)if(auto* v=invoke(position,mark))hub.position=*static_cast<V3*>(runtime->object_unbox(runtime->context,v));
            if(rotation)if(auto* v=invoke(rotation,mark))hub.rotation=*static_cast<V3*>(runtime->object_unbox(runtime->context,v));
            out.push_back(std::move(hub));
        }
        return out;
    }
    // The map screen's official teleport to a hub of this account.
    void officialHubTeleport(const std::string& mapName,V3 destination,const char* purpose) {
        const auto hubs=hubTargets(mapName);
        if(hubs.empty())throw std::runtime_error("本账号在 "+mapName+" 没有可传送的枢纽；请先在该地图解锁或建造枢纽");
        const HubTarget* best=&hubs.front();double bestDistance=1e18;
        for(const auto& hub:hubs){const double d=std::hypot(hub.position.x-destination.x,hub.position.z-destination.z);if(d<bestDistance){bestDistance=d;best=&hub;}}
        int reason=1; // C2STeleportReason.ServerGotoHub (observed from the map screen)
        try {reason=enumValue<int>("C2STeleportReason","ServerGotoHub","Beyond.GEnums");}catch(...) {}
        // Same arguments as the map screen: target level ID (the game derives the
        // scene number from it) and the default teleport UI (with its loading view).
        int ui=enumValue<int>("TeleportUIType","Default","Beyond.Gameplay");int options=0;
        V3 position=best->position,rotation=best->rotation;std::uint32_t node=std::uint32_t(best->node);
        OwnOfficial own(ownOfficialAction);
        api.call(gm("GameAction","TeleportToPosition","Beyond.GEnums.C2STeleportReason|System.String|UnityEngine.Vector3|UnityEngine.Vector3|Beyond.Gameplay.TeleportUIType|Beyond.Gameplay.TeleportOptions|System.Action|System.UInt32","System.Void","Beyond.Gameplay.Actions"),
            nullptr,{&reason,api.string(best->level),&position,&rotation,&ui,&options,nullptr,&node});
        ++officialTeleports;
        officialTeleportStatus={{"purpose",purpose},{"map",mapName},{"hub",best->inst},{"node",best->node},{"level",best->level},
            {"position",{position.x,position.y,position.z}},{"distance_to_target",bestDistance},{"candidates",hubs.size()},{"reason",reason},{"at",nowMs()}};
    }
    void updateEmergencyHome(Net& net) {
        if(!emergencyHomePending)return;
        if(net.currentRole()!=Role::Offline||guestRealm||travel)return; // wait until out of the room and restored
        emergencyHomePending=false;
        try {
            const V3 here=character?api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character):V3{};
            officialHubTeleport("map01",here,"emergency_home");
        }catch(const std::exception& e) {officialTeleportStatus={{"purpose","emergency_home"},{"error",e.what()},{"at",nowMs()}};}
    }
public:
    // Menu: leave any room, then teleport officially to a hub in Valley IV (map01).
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    void setNativeOperationLogger(std::function<void(const std::string&)> logger) {nativeOperationLogger=std::move(logger);}
#endif
    void requestEmergencyHome() {emergencyHomePending=true;officialTeleportStatus={{"purpose","emergency_home"},{"state","queued"},{"at",nowMs()}};}
private:
    void replication(Net& net,Session& link) {
        session=&link;link.update(net);link.setAuthority({});localPeer=net.snapshot().localId;
        // Transport and read-only discovery remain available. No replica world,
        // native enemy creation, account-world withdrawal, shared teleport,
        // actor skill execution or local result projection is admitted here.
        roomHost=guestRealm=guestRestrictionsActive=false;
        replicationError="统一原生权威适配器未验收；共享世界操作已停用";
        if(net.currentRole()==Role::Host&&character&&nativeSceneInitialized&&!mapId.empty()&&!levelId.empty()) {
            auto* info=staticObject("worldInfo");
            auto position=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
            link.publish(net,{{"map",mapId},{"level",levelId},{"level_num",api.value<int>(gm("WorldInfo","get_curLevelIdNum","","System.Int32"),info)},
                {"position",{position.x,position.y,position.z}},{"entities",nlohmann::json::array()} });
        }
    }
    void projectPeers(const Snapshot& view) {
        markers=nlohmann::json::array();if(!character) return;
        auto* camera=api.call(m("Camera","get_main","","UnityEngine.Camera"));if(!camera) return;
        int width=api.value<int>(m("Screen","get_width","","System.Int32")),height=api.value<int>(m("Screen","get_height","","System.Int32"));
        if(width<=0||height<=0) return;
        auto own=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
        for(const auto& p:view.peers) {
            if(p.local||!p.hasInfo||p.states.empty()||nowMs()-p.states.back().receivedMs>3000||!(p.states.back().state.flags&proto::StateValid)) continue;
            const auto& st=p.states.back().state;
            if(p.info.mapId!=mapId||p.info.levelId!=levelId) {markers.push_back({{"id",p.id},{"name",p.info.name},{"other_map",true},{"map",p.info.mapId}});continue;}
            V3 position{st.position[0],st.position[1],st.position[2]};
            if(session)if(auto state=session->nativeState(p.id);state)position={float(state->position[0]),float(state->position[1]),float(state->position[2])};
            float dx=position.x-own.x,dy=position.y-own.y,dz=position.z-own.z;position.y+=2.f;
            auto screen=api.value<V3>(m("Camera","WorldToScreenPoint","UnityEngine.Vector3","UnityEngine.Vector3"),camera,{&position});
            markers.push_back({{"id",p.id},{"name",p.info.name},{"host",p.host},{"other_map",false},
                {"x",screen.x/width},{"y",1.f-screen.y/height},{"behind",screen.z<0},{"distance",std::sqrt(dx*dx+dy*dy+dz*dz)}});
        }
    }

    // ---- local player ----
    void dropLocalModel() {
        localAppearanceInfo=nullptr;lastAppearanceSample=0;appearanceModel.clear();
        for(auto& b:bones) release(b.root);
        bones.clear();boneNames.clear();release(modelRoot);model=nullptr;
    }
    void bindLocalModel(void* go) {
        dropLocalModel();
        model=go;modelRoot=rootOf(go);
        // The Biped skeleton drives the body; finger tips ("Nub") carry no rotation.
        for(auto* transform:components(go,"Transform")) {
            auto name=text(api.call(objectName,transform));
            if(name.rfind("Bip001",0)!=0||name.ends_with("Nub")) continue;
            bones.push_back({transform,rootOf(transform)});boneNames.push_back(name);
            if(bones.size()>=proto::kMaxBones) break;
        }
        ++skeleton;
    }
    void readMap() {
        void* info=nullptr;
        symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(worldInfoField,&info);
        if(!info) {mapId.clear();levelId.clear();mapHash=0;return;}
        mapId=text(api.call(mapIdText,info));levelId=text(api.call(levelIdText,info));
        mapHash=mapId.empty()?0:proto::fnv32(mapId);
    }
    #include "link_world_presentation.h"
    #include "link_world_resources.h"
    void sample(Net& net) {
        const auto now=nowMs();
        if(now-lastMap>1000) {lastMap=now;readMap();}
        auto* entity=api.call(mainCharacter);
        proto::PlayerState state;state.timeMs=std::uint32_t(now);state.mapHash=mapHash;
        void* go=nullptr;
        if(entity) {
            auto* component=api.call(entityModel,entity);
            go=component?api.call(modelGo,component):nullptr;
            if(component&&(entity!=character||go!=model)) localModel=text(api.call(modelPath,component));
        }
        if(!go||!alive(go)) {
            if(model) dropLocalModel();
            character=nullptr;
            // Tell the room once a second that this player is not drawable right now.
            if(now-lastInvalid>1000) {lastInvalid=now;net.setLocalState(state);}
            return;
        }
        if(go!=model) bindLocalModel(go);
        character=entity;
#ifdef EFLINK_NATIVE_DIAGNOSTICS
        static std::uint64_t lastMovementDiagnostics=0;
        if(now-lastMovementDiagnostics>=1000) {
            lastMovementDiagnostics=now;
            try {localMovementDiagnostics=movementState(entity);}catch(const std::exception& e){localMovementDiagnostics={{"error",e.what()}};}
        }
#endif
        if(auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),entity)) {
            localHp=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability);
            localMaxHp=api.value<double>(gm("AbilitySystem","get_maxHp","","System.Double"),ability);
        }
        state.flags=proto::StateValid;
        if(api.value<bool>(entityAlive,entity)) state.flags|=proto::StateAlive;
        if(api.value<bool>(entityCinematic,entity)) state.flags|=proto::StateCinematic;
        try {
            if(auto* weapon=api.call(gm("Entity","get_weaponCom","","Beyond.Gameplay.View.WeaponComponent"),entity)) {
                const int visual=api.value<int>(gm("WeaponComponent","get_weaponState","","Beyond.Gameplay.View.WeaponComponent.WeaponState","Beyond.Gameplay.View"),weapon);
                if(visual>=0&&visual<=5)state.flags|=std::uint8_t(proto::StateWeaponValid|(visual<<5));
            }
        }catch(...) {} // optional presentation metadata never invalidates a pose
        auto* transform=api.call(getTransform,go);
        const auto position=api.value<V3>(getPosition,transform);const auto rotation=api.value<Q>(getRotation,transform);
        state.position[0]=position.x;state.position[1]=position.y;state.position[2]=position.z;
        state.rotation[0]=rotation.x;state.rotation[1]=rotation.y;state.rotation[2]=rotation.z;state.rotation[3]=rotation.w;
        state.skeleton=skeleton;state.bones.reserve(bones.size()*4);
        for(std::size_t i=0;i<bones.size();++i) {
            const auto q=api.value<Q>(getLocalRotation,bones[i].transform);
            for(float v:{q.x,q.y,q.z,q.w}) state.bones.push_back(proto::quantize(v));
            if(i==0) {const auto hips=api.value<V3>(getLocalPosition,bones[i].transform);state.rootOffset[0]=hips.x;state.rootOffset[1]=hips.y;state.rootOffset[2]=hips.z;}
        }
        proto::PlayerInfo info;info.modelPath=localModel;info.mapId=mapId;info.levelId=levelId;info.skeleton=skeleton;info.bones=boneNames;
        net.setLocalInfo(std::move(info));
        net.setLocalState(std::move(state));++samples;
    }

    // ---- remote players ----
    void destroyAvatar(Avatar& a) {
        if(a.object&&!a.native) {try {if(alive(a.object)) api.call(destroy,nullptr,{a.object});} catch(...) {}}
        release(a.root);a.object=a.transform=nullptr;a.bones.clear();a.byName.clear();a.skeleton=0xFFFF;a.visible=false;
        a.native=a.entityPlaced=false;
    }
    void spawn(Avatar& a) {
        // Only operator model resources may be instantiated by a remote peer.
        if(a.modelPath.find("chr_")==std::string::npos||a.modelPath.find("..")!=std::string::npos||a.modelPath.find(':')!=std::string::npos)
            throw std::runtime_error("remote resource is not an operator model");
        a.error.clear();
        void* object=nullptr;
        if(auto actor=actors.find(a.id);actor!=actors.end()&&actor->second.started&&currentActor(actor->second)) {
            auto* component=api.call(entityModel,actor->second.entity);object=component?api.call(modelGo,component):nullptr;
            if(object&&alive(object))a.native=true;
        }
        if(!object) {
        if(!loader) {
            auto klass=api.klass("SimpleAssetLoader","Common.Beyond.dll","Beyond.Resource");
            loader=runtime->object_new(runtime->context,klass.class_info);loaderRoot=rootOf(loader);
            int channel=0,category=0,priority=10; // ELogChannel.None, RootCategory.Main, Default
            api.call(m("SimpleAssetLoader",".ctor","Beyond.ELogChannel|Beyond.Resource.RootCategory|Beyond.Resource.EResourceRequestPriority","System.Void","Common.Beyond.dll","Beyond.Resource"),loader,{&channel,&category,&priority});
        }
        auto* boxedPath=api.call(pathHash,nullptr,{api.string(a.modelPath)});
        if(!boxedPath) throw std::runtime_error("path hash unavailable");
        std::int64_t hash=0;std::memcpy(&hash,runtime->object_unbox(runtime->context,boxedPath),sizeof hash);
        bool worldSpace=false;
        auto* boxedHandle=api.call(instantiate,loader,{&hash,&worldSpace});
        if(!boxedHandle) throw std::runtime_error("model load returned no handle");
        eflink::unity::Api::TemporaryRoot handleRoot(runtime,boxedHandle);
        // Value-type instance methods take the unboxed data as this.
        object=api.call(handleGet,runtime->object_unbox(runtime->context,boxedHandle));
        if(!object) throw std::runtime_error("model not found in game resources: "+a.modelPath);
        }
        a.object=object;a.root=rootOf(object);a.transform=api.call(getTransform,object);
        api.call(m("Object","set_name","System.String","System.Void"),object,{api.string("EF Link · "+a.name)});
        // A copy of the model must not think, aim or collide: animation, IK and gameplay view
        // scripts would fight the network pose, and colliders would block the local player.
        if(!a.native) for(auto* behaviour:components(object,"Behaviour")) {
            const auto type=classOf(behaviour);
            if(type=="UnityEngine.Animator"||type.rfind("RootMotion.",0)==0||type=="Beyond.Gameplay.View.Animation.AnimatorMono"||
               type=="Beyond.Gameplay.View.TransformFollowDamper"||type=="Beyond.Gameplay.View.LookAtSetting") {
                bool off=false;api.call(behaviourEnabled,behaviour,{&off});
            }
        }
        if(!a.native) for(auto* collider:components(object,"Collider","UnityEngine.PhysicsModule.dll")) {bool off=false;api.call(colliderEnabled,collider,{&off});}
        for(auto* transform:components(object,"Transform")) {
            auto name=text(api.call(objectName,transform));
            if(name.rfind("Bip001",0)==0) a.byName.emplace(name,transform);
        }
        a.skeleton=0xFFFF;a.visible=true;
    }
    static Q nlerp(Q a,Q b,float t) {
        if(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0) b={-b.x,-b.y,-b.z,-b.w};
        Q r{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};
        const float n=std::sqrt(r.x*r.x+r.y*r.y+r.z*r.z+r.w*r.w);
        return n>1e-6f?Q{r.x/n,r.y/n,r.z/n,r.w/n}:Q{0,0,0,1};
    }
    static Q boneAt(const proto::PlayerState& s,std::size_t i) {
        return {proto::dequantize(s.bones[i*4]),proto::dequantize(s.bones[i*4+1]),proto::dequantize(s.bones[i*4+2]),proto::dequantize(s.bones[i*4+3])};
    }
    // Keep the room actor's gameplay position (enemy targeting, hit volumes)
    // on the drawn body. No native physics or steering acts on room actors.
    void placeActor(Avatar& a,Actor& actor,V3 position) {
        if(!actor.movement||!currentActor(actor))return;
        if(a.entityPlaced) {
            const float x=position.x-a.entityPosition.x,y=position.y-a.entityPosition.y,z=position.z-a.entityPosition.z;
            if(x*x+y*y+z*z<1e-4f)return;
        }
        bool warning=false;api.call(gm("MovementComponent","TeleportTo","UnityEngine.Vector3|System.Boolean"),actor.movement,{&position,&warning});
        a.entityPosition=position;a.entityPlaced=true;++a.entityMoves;
    }
    void pose(Avatar& a,const PeerView& peer,Actor* placeEntity) {
        if(a.skeleton!=peer.info.skeleton) {
            a.bones.assign(peer.info.bones.size(),nullptr);
            for(std::size_t i=0;i<peer.info.bones.size();++i) if(auto f=a.byName.find(peer.info.bones[i]);f!=a.byName.end()) a.bones[i]=f->second;
            a.skeleton=peer.info.skeleton;
        }
        // Draw 50 ms in the past, between the two snapshots around that moment,
        // using the sender's own clock so network jitter does not shake the body.
        const auto& states=peer.states;
        const auto latestTime=states.back().state.timeMs;
        auto unwrap=[latestTime](std::uint32_t time){return std::int64_t(latestTime)+std::int32_t(time-latestTime);};
        std::int64_t offset=INT64_MAX;
        for(const auto& s:states) offset=(std::min)(offset,std::int64_t(s.receivedMs)-unwrap(s.state.timeMs));
        const std::int64_t target=std::int64_t(nowMs())-50-offset;
        const proto::PlayerState* from=&states.front().state;const proto::PlayerState* to=from;float t=0;
        for(std::size_t i=0;i+1<states.size();++i) {
            const auto& s0=states[i].state;const auto& s1=states[i+1].state;
            const auto time0=unwrap(s0.timeMs),time1=unwrap(s1.timeMs);
            if(time0<=target&&target<=time1) {
                from=&s0;to=&s1;t=time1>time0?float(target-time0)/float(time1-time0):1.f;break;
            }
            if(target>time1) {from=to=&s1;t=0;}
        }
        if(from->skeleton!=to->skeleton||from->bones.size()!=to->bones.size()) from=to;
        const float dx=to->position[0]-from->position[0],dy=to->position[1]-from->position[1],dz=to->position[2]-from->position[2];
        if(dx*dx+dy*dy+dz*dz>64) {from=to;t=0;} // a teleport: jump instead of sliding across the map
        V3 position{from->position[0]+dx*t,from->position[1]+dy*t,from->position[2]+dz*t};
        // The 30 Hz pose stream is the player's own body. The 5 Hz world snapshot
        // is too coarse to draw from: it froze between packets and then jumped.
        if(placeEntity)placeActor(a,*placeEntity,position);
        Q rotation=nlerp({from->rotation[0],from->rotation[1],from->rotation[2],from->rotation[3]},{to->rotation[0],to->rotation[1],to->rotation[2],to->rotation[3]},t);
        api.call(setPosition,a.transform,{&position});api.call(setRotation,a.transform,{&rotation});
        if(to->skeleton!=peer.info.skeleton) return; // pose and bone list from different models
        const auto count=(std::min)(a.bones.size(),to->bones.size()/4);
        for(std::size_t i=0;i<count;++i) {
            if(!a.bones[i]) continue;
            auto q=nlerp(boneAt(*from,i),boneAt(*to,i),t);
            api.call(setLocalRotation,a.bones[i],{&q});
            if(i==0) {
                V3 hips{from->rootOffset[0]+(to->rootOffset[0]-from->rootOffset[0])*t,from->rootOffset[1]+(to->rootOffset[1]-from->rootOffset[1])*t,
                        from->rootOffset[2]+(to->rootOffset[2]-from->rootOffset[2])*t};
                api.call(setLocalPosition,a.bones[i],{&hips});
            }
        }
        ++a.poses;
    }
    void show(Avatar& a,bool value) {
        if(a.object&&a.visible!=value) {api.call(setActive,a.object,{&value});a.visible=value;}
    }
    bool currentAvatar(const Avatar& avatar) {
        if(!avatar.native)return true;
        auto actor=actors.find(avatar.id);
        if(actor==actors.end()||!currentActor(actor->second))return false;
        auto* component=api.call(entityModel,actor->second.entity);
        return component&&api.call(modelGo,component)==avatar.object;
    }
    void updateRemotes(const Snapshot& view,bool allowed) {
        const auto now=nowMs();
        std::set<std::uint16_t> present;
        for(const auto& peer:view.peers) {
            if(peer.local||!peer.hasInfo) continue;
            present.insert(peer.id);
            auto& a=avatars[peer.id];a.id=peer.id;a.name=peer.info.name;
            const bool fresh=!peer.states.empty()&&now-peer.states.back().receivedMs<3000;
            const auto* latest=fresh?&peer.states.back().state:nullptr;
            const bool drawable=allowed&&latest&&(latest->flags&proto::StateValid)&&latest->mapHash==mapHash&&mapHash&&!peer.info.modelPath.empty();
            if(a.native&&!currentAvatar(a))destroyAvatar(a);
            if(a.object&&!alive(a.object)) {a.object=nullptr;release(a.root);a.transform=nullptr;a.byName.clear();a.bones.clear();a.skeleton=0xFFFF;a.visible=false;}
            if(a.object&&a.modelPath!=peer.info.modelPath) destroyAvatar(a);
            if(!a.native&&a.object) {
                auto actor=actors.find(peer.id);
                if(actor!=actors.end()&&actor->second.started&&currentActor(actor->second)) {destroyAvatar(a);a.retryAt=0;}
            }
            if(!drawable) {show(a,false);continue;}
            if(!a.object) {
                if(now<a.retryAt) continue;
                a.modelPath=peer.info.modelPath;
                try {spawn(a);}
                catch(const std::exception& e) {a.error=e.what();a.retryAt=now+15000;if(a.object) destroyAvatar(a);continue;}
            }
            try {show(a,true);auto actor=actors.find(peer.id);
                Actor* owned=a.native&&actor!=actors.end()&&actor->second.started?&actor->second:nullptr;
                // While the host executes an admitted guest's native skill, its own
                // root motion and animation drive that actor until the cast ends.
                if(!(view.role==Role::Host&&owned&&owned->casting))pose(a,peer,owned);}
            catch(const std::exception& e) {a.error=e.what();destroyAvatar(a);a.retryAt=now+5000;}
        }
        for(auto it=avatars.begin();it!=avatars.end();) {
            if(present.contains(it->first)) {++it;continue;}
            destroyAvatar(it->second);it=avatars.erase(it);
        }
    }

    Enemy* ownedTargetController(void* controller) {
        constexpr const char* ai="Beyond.Gameplay.AI";
        auto* entity=readField<void*>(controller,"EnemyAITargetController","m_entity",ai);
        Enemy* owned=nullptr;for(auto& [key,enemy]:enemies)if(enemy.entity==entity&&currentEnemy(enemy)) {owned=&enemy;break;}
        if(!owned)return nullptr;
        auto* component=api.call(gm("Entity","get_enemyAICom","","Beyond.Gameplay.AI.EnemyAIComponent"),entity);
        auto* brain=component?api.call(gm("EnemyAIComponent","get_aiBrain","","Beyond.Gameplay.AI.EnemyAIBrain",ai),component):nullptr;
        return brain&&api.call(gm("EnemyAIBrain","get_targetController","","Beyond.Gameplay.AI.EnemyAITargetController",ai),brain)==controller?owned:nullptr;
    }
    bool roomTargetable(const Actor& actor) {
        return actor.started&&actor.bound&&actor.epoch==session->generation()&&currentActor(actor)&&session->nativeState(actor.peer)&&
            api.value<bool>(entityAlive,actor.entity)&&!api.value<bool>(gm("Entity","get_isDisabled","","System.Boolean"),actor.entity)&&
            !api.value<bool>(gm("Entity","get_isHide","","System.Boolean"),actor.entity)&&!api.value<bool>(entityCinematic,actor.entity);
    }
    // The host's own operator is a room player too; room enemies may fight it.
    bool hostTargetable() {
        return roomHost&&usable(character)&&api.value<bool>(entityAlive,character)&&!api.value<bool>(entityCinematic,character)&&
            !api.value<bool>(gm("Entity","get_isDisabled","","System.Boolean"),character);
    }
    std::vector<void*> roomTargets() {
        std::vector<void*> out;
        for(const auto& [peer,actor]:actors)if(roomTargetable(actor))out.push_back(actor.entity);
        if(hostTargetable())out.push_back(character);
        return out;
    }
public:
    // Used by the separate read-only resource inspector, on the native game thread.
    // This does not tick the room adapter or execute any gameplay operation.
    nlohmann::json inspectNativeResources(const BE_HostApiV1* host) {
        runtime=host;api.bind(host);assembly=GetModuleHandleW(L"GameAssembly.dll");resolve();
        auto* info=staticObject("worldInfo");auto* level=staticObject("levelLoader");
        if(!info||!level||!api.value<bool>(gm("WorldInfo","get_isStarted","","System.Boolean"),info)||
           api.value<bool>(gm("GameLevelLoader","get_isLoading","","System.Boolean"),level))throw std::runtime_error("native scene is not ready for resource inspection");
        mapId=text(api.call(mapIdText,info));levelId=text(api.call(levelIdText,info));
        nlohmann::json resources=nlohmann::json::array();
        nlohmann::json properties;
        try {for(const auto& value:sampleResources()) {auto state=value.state;state["key"]=value.key;state["kind"]=value.kind;resources.push_back(std::move(state));}properties=resourceProperties();}
        catch(...) {clearResources();throw;}
        auto types=resourceNativeTypes;auto unknown=resourceUnknown;clearResources();
        return {{"map",mapId},{"level",levelId},{"resources",resources},{"properties",properties},{"native_types",types},{"unadapted",unknown},{"gameplay_modified",false}};
    }
    const nlohmann::json& markerSnapshot() const {return markers;}
    bool takeHostMapNotice() {return hostMapConsent.takeNotice();}
    void chooseHostMap(Net& net,std::uint64_t token,bool follow) {
        if(net.currentRole()!=Role::Client||!session||!session->followHost)throw std::runtime_error("当前不是跟随房主的房客");
        if(follow) {
            const auto blockers=session->entryStatus().at("blockers");
            if(std::find(blockers.begin(),blockers.end(),nlohmann::json("host_scene_rules"))!=blockers.end())
                throw std::runtime_error("房主当前场景尚未开放联机入场");
        }
        hostMapConsent.choose(token,follow);if(follow){travelState="queued";replicationError.clear();}
    }
    void damageHookSeen() {++damageHookCalls;}
    // Testing aid (menu, not saved): damage to this player's own operators is
    // discarded. Remote players' stand-ins and monsters are unaffected.
    bool testingInvincible=false;std::uint64_t invincibleBlocked=0;
    bool blocksLocalDamage(void* ability) {
        if(!testingInvincible||!ability)return false;
        try {
            auto* entity=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),ability);
            if(!entity)return false;
            for(const auto& [peer,actor]:actors)if(actor.entity==entity)return false;
            auto* root=api.call(gm("Entity","get_rootCom","","Beyond.Gameplay.Core.RootComponent"),entity);
            if(!root||classOf(root)!="Beyond.Gameplay.Core.CharacterRootComponent")return false;
            ++invincibleBlocked;return true;
        }catch(...) {return false;}
    }
    // Receive filter (guest in a native room): this account's monster enter/leave
    // messages are withheld (kept in order for restoration on leave); messages
    // carrying the player's own characters always pass.
    bool filtering() const {return nativeFeedAdapter&&feedFilterActive&&(guestRealm||restorationPending)&&!feedOperating;}
    // 0 outside any room, 1 guest inside the host world, 2 hosting a room.
    int outboundScope() const {return filtering()?1:roomHost?2:0;}
    void resetAppearance() {
        officialAppearance.clear();localAppearanceInfo=nullptr;lastAppearanceSample=0;appearanceModel.clear();appearanceError.clear();
    }
    void observeAppearance(const std::string& type,const std::string& bytes) {
        officialAppearance.observe(type,bytes);localAppearanceInfo=nullptr;lastAppearanceSample=0;
    }
    void rejectAppearance() {
        officialAppearance.reject("official appearance serialization failed");localAppearanceInfo=nullptr;lastAppearanceSample=0;
    }
    const std::set<std::uint64_t>& fedIds() const {return nativeOpPolicy.fedIds();}
    // Every official monster enter/leave view (in or out of a room) keeps the
    // exact server data, so the player's own world can be restored verbatim.
    void observeOfficial(const std::string& type,const std::string& bytes) {
        if(!nativeFeedAdapter)return;
        if(type=="SC_OBJECT_ENTER_VIEW") {
            fields(bytes,[&](int f,int w,const std::string& detail,std::uint64_t){
                if(f==1&&w==2)fields(detail,[&](int g,int x,const std::string& monster,std::uint64_t){
                    if(g==2&&x==2) {
                        std::uint64_t id=0;
                        fields(monster,[&](int h,int y,const std::string& common,std::uint64_t){
                            if(h==1&&y==2)fields(common,[&](int k,int z,const std::string&,std::uint64_t v){if(k==1&&z==0)id=v;return k!=1;});
                            return !id;});
                        if(id&&(officialMonsters.size()<4096||officialMonsters.contains(id)))officialMonsters[id]=monster;
                    }
                    return true;});
                return true;});
        } else if(type=="SC_OBJECT_LEAVE_VIEW") {
            fields(bytes,[&](int f,int w,const std::string& item,std::uint64_t){
                if(f==1&&w==2) {std::uint64_t id=0;fields(item,[&](int g,int x,const std::string&,std::uint64_t v){if(g==2&&x==0)id=v;return true;});officialMonsters.erase(id);}
                return true;});
        }
    }
    // Guest in the host world: battle operations of host monsters (and side
    // effects of our own enter/leave calls) are not created at all, so they
    // never reach this account's official server and consume no sequence IDs.
    // A host monster shown here never has a battle instance of this account
    // (battleInstId 0); its operations stay local even while the native
    // pipeline is still unloading it after the player left the room.
    bool suppressServerOp(std::uint64_t serverId,std::uint64_t battleInstance) {
        if(!nativeFeedAdapter)return false;
        // EF Link's own stand-in actors for other players (0xEF.. room IDs) are
        // never battle entities of this account either.
        // Inside the host world a host monster can still be handed a battle
        // instance by this client during combat; its source ID alone decides.
        const bool suppress=(guestRealm&&feedOperating)||nativeOpPolicy.suppress(serverId,battleInstance)||
            (filtering()&&serverId&&nativeOpPolicy.fedIds().contains(serverId));
        if(suppress)++opsSuppressed;
        else if(battleInstance==0&&guestRealm&&leakedOwners.size()<64)++leakedOwners[serverId];
        return suppress;
    }
    bool withholdOfficial(const std::string& type,std::string bytes) {
        if(!filtering())return false;
        if(!withheldOfficial.append(type,std::move(bytes))) {
            recoveryAbort=true;++feedErrors;feedError="官方世界消息暂存达到上限，已撤销房间权限并准备恢复个人世界";
            if(session)session->setLocalSceneReady(false);
            throw std::runtime_error(feedError);
        }
        ++withheldCount;return true;
    }
    static bool readVarint(const std::string& b,std::size_t& at,std::uint64_t& v) {
        v=0;for(int shift=0;shift<64&&at<b.size();shift+=7){const auto c=static_cast<unsigned char>(b[at++]);v|=std::uint64_t(c&0x7f)<<shift;if(!(c&0x80))return true;}
        return false;
    }
    static bool fields(const std::string& b,const std::function<bool(int,int,const std::string&,std::uint64_t)>& visit) {
        std::size_t at=0;
        while(at<b.size()) {
            std::uint64_t key=0,value=0;if(!readVarint(b,at,key))return false;
            const int field=int(key>>3),wire=int(key&7);std::string payload;
            if(wire==0){if(!readVarint(b,at,value))return false;}
            else if(wire==1){if(at+8>b.size())return false;at+=8;}
            else if(wire==5){if(at+4>b.size())return false;at+=4;}
            else if(wire==2){if(!readVarint(b,at,value)||at+value>b.size())return false;payload=b.substr(at,std::size_t(value));at+=std::size_t(value);}
            else return false;
            if(!visit(field,wire,payload,value))return true;
        }
        return true;
    }
    void setGuestGates(bool ready,std::string error) {guestGatesReady=ready;guestGateError=std::move(error);}
    void setNativeEvidence(bool combatReady,bool isolationReady) {nativeCombatReady=combatReady;nativeIsolationReady=isolationReady;}
    void setWeaponLookupEvidence(bool ready) {nativeWeaponLookupReady=ready;}
    void* roomCharacterInfo(std::uint64_t id) {
        if(!session||!session->ready()||(id>>56)!=0xEF)return nullptr;
        for(const auto& [peer,actor]:actors)if(actor.id==id&&(peer>=0x8000||actor.id==session->playerEntity(peer))&&actor.epoch==session->generation()&&actor.info&&currentActor(actor)) {++roomCharacterLookups;return actor.info;}
        return nullptr;
    }
    bool roomWeaponModelPath(std::uint64_t id,void** output,bool& handled) {
        auto* info=roomCharacterInfo(id);handled=info!=nullptr;
        if(!handled||!output)return false;
        *output=nullptr;
        auto* weapon=readField<void*>(info,"ClientCharInfo","m_weaponData","Beyond.Gameplay");
        if(!weapon)return false;
        const auto name=text(readField<void*>(weapon,"InventorySystem/ItemInstData","templateId","Beyond.Gameplay"));
        if(name.empty()||name.size()>120)return false;
        bool overrideModel=false;
        return api.value<bool>(gm("WeaponUtil","TryGetWeaponModelByTemplateId","System.String|System.Boolean|System.String&","System.Boolean","Beyond.Gameplay"),nullptr,
            {api.string(name),&overrideModel,output});
    }
    bool privateActionsRestricted(Net& net) const {
        if(!roomAdapterAvailable)return false;
        return restorationPending||(net.currentRole()==Role::Client&&session&&session->followHost)||home.has_value()||(travel&&travel->homeward);
    }
    void requireTargetGate(bool ready) {if(!ready){guestGatesReady=false;guestGateError+="battle_target_filter; ";}}
    bool allowRoomTarget(Net& net,void* ability) {
        if(!ability||net.currentRole()==Role::Offline||!session||!session->followHost)return true;
        if(!guestRealm)return true;
        auto* entity=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),ability);
        if(auto it=privateEnemies.find(entity);it!=privateEnemies.end()&&currentEnemy(it->second)) {
            ++blockedGuestActions["private_enemy_target"];return false;
        }
        if(net.currentRole()==Role::Client) {
            for(const auto& [key,enemy]:enemies)if(enemy.entity==entity&&roomEnemy(key)&&currentEnemy(enemy))return true;
            ++blockedGuestActions["private_object_target"];return false;
        }
        return true;
    }
    // EF Link's own official moves (follow, teleport to a player, emergency home)
    // pass the guest restrictions; the player's own private ones stay blocked.
    int ownOfficialAction=0;
    struct OwnOfficial {int& depth;explicit OwnOfficial(int& d):depth(d){++depth;}~OwnOfficial(){--depth;}};
    bool blockPrivateAction(Net& net,const char* action) {
        if(ownOfficialAction>0)return false;
        if(!privateActionsRestricted(net))return false;
        ++blockedGuestActions[action];return true;
    }
    bool roomEnemyAssignTarget(Net& net,void* controller,void*& target) {
        if(!controller||net.currentRole()!=Role::Host||!session||!session->ready()||!roomHost||!character)return false;
        Enemy* owned=nullptr;
        try {
            owned=ownedTargetController(controller);if(!owned)return false;
            ++owned->targetAssignments;
            // Native AI may clear its target on mode changes or a lost sightline.
            // Preserve that lifecycle rather than forcing continuous aggression.
            if(!target)return true;
            for(const auto& [peer,actor]:actors)if(actor.entity==target) {
                if(!roomTargetable(actor))target=nullptr;
                return true;
            }
            if(target==character) {if(!hostTargetable())target=nullptr;return true;}
            // Room NPCs must not acquire an account-world character via an AI
            // mode/group fallback. Select from room actors using native geometry.
            ++owned->foreignTargetsRejected;void* replacement=nullptr;int range=0;
            selectRoomEnemyTarget(net,controller,replacement,range);target=replacement;
            return true;
        }catch(const std::exception& e){if(owned){owned->targetError=e.what();target=nullptr;return true;}return false;}
    }
    bool roomEnemyFindEnabled(Net& net,void* controller,bool& enabled) {
        if(!controller||net.currentRole()!=Role::Host||!session||!session->ready()||!roomHost||!character)return false;
        Enemy* owned=nullptr;
        try {
            owned=ownedTargetController(controller);if(!owned)return false;
            ++owned->enableQueries;enabled=false;
            constexpr const char* ai="Beyond.Gameplay.AI";
            const auto here=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),owned->entity);
            const float radius=(std::max)(enumValue<float>("EnemyAITargetController","MAX_ACTIVE_DIS",ai),readField<float>(controller,"EnemyAITargetController","m_longSightRange",ai));
            if(!std::isfinite(radius)||radius<0)throw std::runtime_error("房间怪物原生激活距离无效");
            for(auto* candidate:roomTargets()) {
                const auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),candidate);
                const double dx=p.x-here.x,dy=p.y-here.y,dz=p.z-here.z;
                if(dx*dx+dy*dy+dz*dz<=double(radius)*radius) {enabled=true;break;}
            }
            return true;
        }catch(const std::exception& e){if(owned){owned->targetError=e.what();enabled=false;return true;}return false;}
    }
    bool selectRoomEnemyTarget(Net& net,void* controller,void*& selected,int& range) {
        if(!controller||net.currentRole()!=Role::Host||!session||!session->ready()||!roomHost||!character)return false;
        Enemy* owned=nullptr;
        try {
            constexpr const char* ai="Beyond.Gameplay.AI";
            owned=ownedTargetController(controller);
            if(!owned)return false;
            auto* entity=owned->entity;
            selected=nullptr;range=enumValue<int>("EnemyAITargetController/TargetRange","None",ai);++owned->targetQueries;
            const auto here=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),entity);
            float shortRadius=readField<float>(controller,"EnemyAITargetController","m_shortSightRange",ai),
                shortH=readField<float>(controller,"EnemyAITargetController","m_shortHFOV",ai),shortV=readField<float>(controller,"EnemyAITargetController","m_shortVFOV",ai),
                longRadius=readField<float>(controller,"EnemyAITargetController","m_longSightRange",ai),
                longH=readField<float>(controller,"EnemyAITargetController","m_longHFOV",ai),longV=readField<float>(controller,"EnemyAITargetController","m_longVFOV",ai),
                circleRadius=readField<float>(controller,"EnemyAITargetController","m_innerCircleRange",ai);
            auto sector=gm("EnemyAITargetController","_FindTargetInSector","Beyond.Gameplay.Core.Entity|System.Single|System.Single|System.Single","System.Boolean",ai);
            auto circle=gm("EnemyAITargetController","_FindTargetInCircle","Beyond.Gameplay.Core.Entity|System.Single|System.Single","System.Boolean",ai);
            // The original selector only tests the private account's main character.
            // Our candidates are actual bound room entities; retain the game's own
            // range, sector, vertical angle and line-of-sight checks for each one.
            double nearest=std::numeric_limits<double>::infinity();std::uint64_t chosen=0;
            for(auto* candidate:roomTargets()) {
                int candidateRange=0;
                if(api.value<bool>(sector,controller,{candidate,&shortRadius,&shortH,&shortV}))
                    candidateRange=enumValue<int>("EnemyAITargetController/TargetRange","Short",ai);
                else {float circleVertical=(std::max)(shortV,longV);
                    if(api.value<bool>(circle,controller,{candidate,&circleRadius,&circleVertical}))candidateRange=enumValue<int>("EnemyAITargetController/TargetRange","Circle",ai);
                    else if(api.value<bool>(sector,controller,{candidate,&longRadius,&longH,&longV}))candidateRange=enumValue<int>("EnemyAITargetController/TargetRange","Long",ai);
                }
                if(!candidateRange)continue;
                auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),candidate);
                const double dx=p.x-here.x,dy=p.y-here.y,dz=p.z-here.z,distance=dx*dx+dy*dy+dz*dz;
                if(distance<nearest) {
                    nearest=distance;selected=candidate;range=candidateRange;chosen=0;
                    for(const auto& [peer,actor]:actors)if(actor.entity==candidate)chosen=actor.id;
                    if(candidate==character&&session)chosen=session->playerEntity(net.snapshot().localId);
                }
            }
            owned->targetActor=chosen;owned->targetRange=range;owned->targetError.clear();return true;
        }catch(const std::exception& e) {
            if(owned) {selected=nullptr;range=0;owned->targetActor=0;owned->targetError=e.what();return true;}
            return false;
        }
    }
    void restoreBackground() {
        if(!backgroundLease)return;
        if(api.value<bool>(m("Application","get_runInBackground","","System.Boolean")))api.call(m("Application","set_runInBackground","System.Boolean","System.Void"),nullptr,{&backgroundOriginal});
        backgroundLease=false;
    }
    struct DamageObservation {void* ability=nullptr;std::uint64_t actor=0;std::string key;double hp=0;void* target=nullptr;NativeIdentity identity;bool playerTarget=false;std::uint16_t peer=0;};
    std::optional<DamageObservation> beforeNativeDamage(Net& net,void* ability,void* modifier) {
        if(net.currentRole()!=Role::Host||!roomHost||!ability||!modifier)return {};
        try {
            if(!api.value<bool>(gm("AbilitySystem/Modifier","get_isDamage","","System.Boolean"),modifier))return {};
            auto* target=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),ability);
            for(const auto& [key,enemy]:enemies)if(enemy.entity==target&&currentEnemy(enemy))
                return DamageObservation{ability,0,key,api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability),target,enemy.identity};
        }catch(...) {}return {};
    }
    void afterNativeDamage(Net& net,const DamageObservation& observation,int result) {
        try {
            if(!currentEntity(observation.target,observation.identity))return;
            if(api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),observation.ability)!=observation.target)return;
            auto hp=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),observation.ability);
            if(observation.actor) {
                if(nativeHits.size()>=32)nativeHits.erase(nativeHits.begin());
                nativeHits.push_back({{"source_actor",observation.actor},{"key",observation.key},{"native_result",result},
                    {"target_actor",observation.playerTarget?observation.identity.server:std::uint64_t{}},{"player_target",observation.playerTarget},
                    {"hp_before",observation.hp},{"hp_after",hp},{"at",nowMs()}});
            }
            if(roomHost&&!observation.playerTarget) {
                auto state=enemyState(observation.target,observation.key);
                if(!state.empty()) {
                    auto old=hostEnemySeeds.find(observation.key);
                    if(old==hostEnemySeeds.end()||old->second.value("alive",true))hostEnemySeeds[observation.key]=std::move(state);
                }
            }
        }catch(...) {}
    }
    void* aiTargetOf(void* entity) {
        constexpr const char* ai="Beyond.Gameplay.AI";
        auto* component=api.call(gm("Entity","get_enemyAICom","","Beyond.Gameplay.AI.EnemyAIComponent"),entity);
        auto* brain=component?api.call(gm("EnemyAIComponent","get_aiBrain","","Beyond.Gameplay.AI.EnemyAIBrain",ai),component):nullptr;
        auto* controller=brain?api.call(gm("EnemyAIBrain","get_targetController","","Beyond.Gameplay.AI.EnemyAITargetController",ai),brain):nullptr;
        return controller?api.call(gm("EnemyAITargetController","GetTargetNoCache","","Beyond.Gameplay.Core.Entity",ai),controller):nullptr;
    }
    // Run or suspend an enemy's native AI and controller ticks (owner vs puppet).
    // A puppet keeps its controller ticking: it walks to reported positions with
    // the enemy's own locomotion animation instead of sliding.
    void setEnemyAI(Enemy& e,bool run) {
        if(!e.controllerPaused&&((run&&!e.aiPaused)||(!run&&e.aiPaused)))return;
        auto* ai=api.call(gm("Entity","get_enemyAICom","","Beyond.Gameplay.AI.EnemyAIComponent"),e.entity);
        auto* ctrl=api.call(gm("Entity","get_enemyCtrl","","Beyond.Gameplay.Core.EnemyController"),e.entity);
        if(e.controllerPaused&&ctrl)api.call(gm("TickComponent","ResumeTick"),ctrl);
        e.controllerPaused=false;
        if(run) {
            if(e.aiPaused&&ai)api.call(gm("TickComponent","ResumeTick"),ai);
            e.aiPaused=false;
        } else if(!e.aiPaused&&ai&&api.value<bool>(gm("TickComponent","IsTicking","","System.Boolean"),ai)) {
            api.call(gm("TickComponent","PauseTick"),ai);e.aiPaused=true;
            if(ctrl)api.call(gm("EnemyController","StopMove"),ctrl);
        }
    }
    std::uint16_t ownerOf(const std::string& key) const {
        auto it=owners.find(key);return it==owners.end()?std::uint16_t(proto::kHostId):it->second;
    }
    std::uint16_t peerOfEntity(void* entity) const {
        if(!entity)return 0;
        if(entity==character)return localPeer;
        for(const auto& [id,actor]:actors)if(actor.entity==entity)return id;
        return 0;
    }
    std::uint16_t castTarget(void* targetHandle) {
        if(!targetHandle)return 0;
        auto* view=runtime->object_new(runtime->context,api.klass("TargetHandleView","Gameplay.Beyond.dll","Beyond.Gameplay.Core").class_info);
        eflink::unity::Api::TemporaryRoot viewRoot(runtime,view);auto* raw=runtime->object_unbox(runtime->context,view);
        api.call(gm("TargetHandleView",".ctor","Beyond.Gameplay.Core.TargetHandle"),raw,{targetHandle});
        auto* first=api.call(gm("TargetHandleView","get_firstTarget","","Beyond.Gameplay.Core.AbilitySystem"),raw);
        return peerOfEntity(first?api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),first):nullptr);
    }
    void clearPlayerCastObservations() {
        for(auto& [skill,cast]:observedPlayerCasts)release(cast.root);
        observedPlayerCasts.clear();
        nativePlayerCastBindings.clear();
    }
    void observePlayerCastBegin(Net& net,void* ability,void* skillString,void* targetHandle) {
        try {
            if(replayingCast||!session||!character||!session->canObservePlayer(net.snapshot().localId)||!ability)return;
            auto* entity=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),ability);
            if(entity!=character)return; // remote casts and native enemy replays are not re-announced
            const auto name=text(skillString);void* skill=nullptr;
            if(name.empty()||name.size()>128||!api.value<bool>(gm("AbilitySystem","TryGetSkill","System.String|Beyond.Gameplay.Core.Skill&","System.Boolean"),ability,{skillString,&skill})||!skill)return;
            const auto nativeId=api.value<std::uint32_t>(gm("Skill","get_skillCastId","","System.UInt32"),skill);
            if(auto old=observedPlayerCasts.find(skill);old!=observedPlayerCasts.end()) {
                if(old->second.nativeId==nativeId&&old->second.actor==session->playerEntity(net.snapshot().localId)&&old->second.epoch==session->generation())return;
                auto terminal=old->second.event;
                terminal["phase"]=old->second.pendingFinish==enumValue<int>("Skill/FinishType","Completed")?"end":"interrupt";
                if(session->publishPlayerCast(net,terminal))++playerCastEventsSent;
                release(old->second.root);observedPlayerCasts.erase(old);
            }
            if(observedPlayerCasts.size()>=64)return;
            std::string key;std::uint64_t life=0;
            if(targetHandle) {
                auto* view=runtime->object_new(runtime->context,api.klass("TargetHandleView","Gameplay.Beyond.dll","Beyond.Gameplay.Core").class_info);
                eflink::unity::Api::TemporaryRoot viewRoot(runtime,view);auto* raw=runtime->object_unbox(runtime->context,view);
                api.call(gm("TargetHandleView",".ctor","Beyond.Gameplay.Core.TargetHandle"),raw,{targetHandle});
                auto* first=api.call(gm("TargetHandleView","get_firstTarget","","Beyond.Gameplay.Core.AbilitySystem"),raw);
                auto* target=first?api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),first):nullptr;
                for(const auto& [id,enemy]:enemies)if(enemy.entity==target&&currentEnemy(enemy)) {
                    auto reference=session->entityReference(id);if(!reference.is_null()) {key=id;life=reference.at("life").get<std::uint64_t>();}break;
                }
            }
            if(playerCastSequence==UINT64_MAX)return;
            auto event=nlohmann::json{{"cast",++playerCastSequence},{"phase","begin"},{"skill",name},{"key",key},{"life",life}};
            if(!session->publishPlayerCast(net,event))return;
            if(!nativePlayerCastBindings.bind(reinterpret_cast<std::uintptr_t>(skill),nativeId,net.snapshot().roomId,
                session->playerEntity(net.snapshot().localId),session->generation(),event.at("cast").get<std::uint64_t>(),identityOf(entity),nowMs()))
                nativePlayerHitError="原生施法身份不可绑定，命中声明已停用";
            observedPlayerCasts[skill]={skill,rootOf(skill),nativeId,session->playerEntity(net.snapshot().localId),session->generation(),nowMs(),identityOf(entity),entity,event};
            lastPlayerCast=event;++playerCastEventsSent;
        }catch(const std::exception& e){replicationError=e.what();}
    }
    struct PlayerCastEndObservation {void* skill=nullptr;std::uint64_t cast=0,actor=0,epoch=0;std::uint32_t nativeId=0;};
    std::optional<PlayerHitSource> observePlayerHitSource(Net& net,void* modifier) {
        if(!session||!modifier||!character||!session->canObservePlayer(net.snapshot().localId))return {};
        try {
            if(!api.value<bool>(gm("AbilitySystem/Modifier","get_isDamage","","System.Boolean"),modifier))return {};
            auto* source=api.call(gm("AbilitySystem/Modifier","get_source","","Beyond.Gameplay.Core.AbilitySystem"),modifier);
            auto* entity=source?api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),source):nullptr;
            if(entity!=character)return {}; // summon/teammate attribution needs its own adapter
            auto* info=api.call(gm("AbilitySystem/Modifier","get_skillCastInfo","","Beyond.Gameplay.Core.Skill.SkillCastInfo"),modifier);
            if(!info)throw std::runtime_error("原生命中没有施法资料");
            eflink::unity::Api::TemporaryRoot infoRoot(runtime,info);
            auto* skill=readField<void*>(info,"Skill/SkillCastInfo","originSkill");
            const auto nativeId=readField<std::uint32_t>(info,"Skill/SkillCastInfo","skillCastId");
            if(!skill||!nativeId||api.call(gm("Skill","get_owner","","Beyond.Gameplay.Core.AbilitySystem"),skill)!=source)
                throw std::runtime_error("命中来源不属于该原生施法角色");
            const auto actor=session->playerEntity(net.snapshot().localId),epoch=session->generation();
            auto cast=nativePlayerCastBindings.resolve(reinterpret_cast<std::uintptr_t>(skill),nativeId,net.snapshot().roomId,actor,epoch,identityOf(entity),nowMs());
            if(!cast)throw std::runtime_error("原生命中无法关联房间施法实例");
            const auto action=api.value<int>(gm("AbilitySystem/Modifier","get_serverActionIndex","","System.Int32"),modifier);
            const auto unit=api.value<int>(gm("AbilitySystem/Modifier","get_damageUnitIndex","","System.Int32"),modifier);
            if(action<0||unit<0||action>1000000||unit>1000000)throw std::runtime_error("命中单元编号尚未适配");
            auto skillId=text(api.call(gm("Skill","get_skillId","","System.String"),skill));
            lastPlayerHitSource={{"actor",actor},{"epoch",epoch},{"cast",*cast},{"skill",skillId},{"action",action},{"unit",unit}};
            ++nativePlayerHitMatched;nativePlayerHitError.clear();return PlayerHitSource{actor,epoch,*cast,action,unit,skillId};
        }catch(const std::exception& e){++nativePlayerHitMissing;nativePlayerHitError=e.what();return {};}
    }
    std::optional<PlayerCastEndObservation> beforePlayerCastEnd(void* skill,int finish) {
        auto it=observedPlayerCasts.find(skill);if(it==observedPlayerCasts.end())return {};
        try {
            if(!currentEntity(it->second.entity,it->second.source)||
                api.value<std::uint32_t>(gm("Skill","get_skillCastId","","System.UInt32"),skill)!=it->second.nativeId)return {};
            it->second.pendingFinish=finish;
            return PlayerCastEndObservation{skill,it->second.event.at("cast").get<std::uint64_t>(),it->second.actor,it->second.epoch,it->second.nativeId};
        }catch(...) {return {};}
    }
    void afterPlayerCastEnd(Net& net,const PlayerCastEndObservation& observed,int finish) {
        auto it=observedPlayerCasts.find(observed.skill);if(it==observedPlayerCasts.end())return;
        // Native end callbacks may immediately begin another cast on the same
        // pooled Skill object. Never terminate that replacement instance.
        if(it->second.event.at("cast")!=observed.cast||it->second.actor!=observed.actor||it->second.epoch!=observed.epoch||it->second.nativeId!=observed.nativeId)return;
        try {
            if(session&&it->second.epoch==session->generation()&&it->second.actor==session->playerEntity(net.snapshot().localId)) {
                const bool interrupted=finish==enumValue<int>("Skill/FinishType","Interrupted");
                if(!interrupted&&finish!=enumValue<int>("Skill/FinishType","Completed"))throw std::runtime_error("未适配的原生技能结束类型");
                auto event=it->second.event;event["phase"]=interrupted?"interrupt":"end";
                if(session->publishPlayerCast(net,event)) {++playerCastEventsSent;lastPlayerCast=event;}
            }
        }catch(const std::exception& e){replicationError=e.what();}
        release(it->second.root);observedPlayerCasts.erase(it);
    }
    // A room enemy whose AI runs here started a native skill: tell the others,
    // who replay it on their puppet. Replays themselves are never re-announced.
    void observeEnemyCast(Net& net,void* ability,void* skillString,void* targetHandle) {
        try {
            if(replayingCast||!session||!session->ready()||!ability)return;
            const auto role=net.currentRole();
            if(role!=Role::Host||!roomHost)return;
            auto* entity=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),ability);
            std::string key;for(const auto& [id,e]:enemies)if(e.entity==entity&&currentEnemy(e)&&roomEnemy(id)) {key=id;break;}
            if(key.empty()||ownerOf(key)!=localPeer)return;
            const auto skill=text(skillString);if(skill.empty()||skill.size()>128)return;
            const auto peer=castTarget(targetHandle);
            if(session->sendEnemyCast(net,key,skill,peer))++enemyCastsSent;
        }catch(const std::exception& e){replicationError=e.what();}
    }
    // Replay another machine's enemy skill on the local puppet, aimed at the
    // same player, so its attack animation and effects play here too. Damage
    // from a puppet is blocked; the owner's machine resolves the real hit.
    bool replayEnemyCast(const std::string&,const std::string&,std::uint16_t) {
        return false; // Native gameplay skills are never replayed as display effects.
    }
    void mirrorEnemyCasts(Net& net,Session& link) {
        for(const auto& cast:link.takeEnemyCasts()) {
            try {
                if(!link.canAct(net.snapshot().localId)) {++enemyCastsRejected;continue;}
                const auto key=cast.at("key").get<std::string>(),skill=cast.at("skill").get<std::string>();
                const auto target=cast.at("target").get<std::uint16_t>(),from=cast.value("from",std::uint16_t(proto::kHostId));
                if(!link.entityMatches(key,cast.at("life").get<std::uint64_t>(),cast.at("lease").get<std::uint64_t>())) {++enemyCastsRejected;continue;}
                if(net.currentRole()==Role::Host) {
                    // Only the enemy's current owner may announce its attacks.
                    if(ownerOf(key)!=from||from==localPeer){++enemyCastsRejected;continue;}
                    link.sendEnemyCast(net,key,skill,target);
                } else if(ownerOf(key)==localPeer) continue; // our own enemy, relayed back
                if(replayEnemyCast(key,skill,target))++enemyCastsMirrored;else ++enemyCastsRejected;
            }catch(const std::exception& ex){++enemyCastsRejected;replicationError=ex.what();}
        }
    }
    void pushMotion(const std::string& key,std::uint64_t stamp,V3 position,Q rotation) {
        auto& m=motions[key];if(stamp<=m.stamp)return;
        if(!m.samples.empty()) {
            const auto& [at,p,q]=m.samples.back();const float dx=position.x-p.x,dy=position.y-p.y,dz=position.z-p.z;
            if(stamp-at>1000||dx*dx+dy*dy+dz*dz>100)m.samples.clear(); // a jump, not a walk
        }
        m.stamp=stamp;m.samples.emplace_back(stamp,position,rotation);while(m.samples.size()>8)m.samples.pop_front();
    }
    // Every frame: interpolate puppet enemies 100 ms behind their 20 Hz reports.
    std::vector<authority::MoveSample> nativeMoves;
    void flushNativeMoves() {
        if(nativeMoves.empty())return;
        try {FeedScope scope(feedOperating);feed.bind(runtime);feed.move(nativeMoves,nativeMoveNotify);}
        catch(const std::exception& e) {++nativeMoveErrors;nativeMoveError=e.what();}
        nativeMoves.clear();
        for(auto& [entity,p]:nativePins)try {
            if(auto* movement=api.call(gm("Entity","get_movementComponent","","Beyond.Gameplay.Core.MovementComponent"),entity)) {
                bool warning=false;api.call(gm("MovementComponent","TeleportTo","UnityEngine.Vector3|System.Boolean"),movement,{&p,&warning});
            }
        }catch(...) {++pinErrors;}
        nativePins.clear();
    }
    void updateEnemyMotion() {
        const auto now=nowMs();if(now<100)return;const auto target=now-100;
        if(traceFrames>0&&!traceId&&character) {
            // nearest host monster to this player (game thread)
            double best=1e9;const V3 self=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
            for(const auto& [key,s]:spawnedEnemies)if(s.native)if(auto e=enemies.find(key);e!=enemies.end()&&currentEnemy(e->second)) {
                const auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),e->second.entity);
                const double d=std::hypot(p.x-self.x,p.z-self.z);if(d<best){best=d;traceId=s.serverId;}
            }
        }
        struct Flush {World& w;std::uint64_t now;~Flush(){if(now-w.lastNativeMove>=w.nativeMoveIntervalMs||w.nativeMoves.size()>=64){w.lastNativeMove=now;w.flushNativeMoves();}else {w.nativeMoves.clear();w.nativePins.clear();}}} flush{*this,now};
        for(auto& [key,m]:motions) {
            if(m.samples.empty())continue;
            auto e=enemies.find(key);if(e==enemies.end()||!currentEnemy(e->second))continue;
            if(e->second.mirroredCastAt&&!nativeMovePuppets) {
                if(now-e->second.mirroredCastAt<50)continue; // Allow the native timeline's first tick.
                // Preserve actual native skill root motion, then resume as soon
                // as the cast ends; a fixed timeout stalls short casts and can
                // interrupt longer ones.
                if(now-e->second.lastCastCheck>=50) {
                    auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),e->second.entity);
                    e->second.mirroredCasting=ability&&anySkillCasting(ability);
                    e->second.lastCastCheck=now;
                }
                if(e->second.mirroredCasting)continue;
                e->second.mirroredCastAt=0;
            }
            auto [t0,p0,q0]=m.samples.front();V3 p=p0;Q q=q0;
            for(std::size_t i=1;i<m.samples.size();++i) {
                const auto& [ta,pa,qa]=m.samples[i-1];const auto& [tb,pb,qb]=m.samples[i];
                if(target>=tb) {p=pb;q=qb;continue;}
                if(target<=ta)break;
                const float t=float(target-ta)/float(tb-ta);
                p={pa.x+(pb.x-pa.x)*t,pa.y+(pb.y-pa.y)*t,pa.z+(pb.z-pa.z)*t};q=nlerp(qa,qb,t);break;
            }
            auto* entity=e->second.entity;
            if(nativeMovePuppets) if(auto s=spawnedEnemies.find(key);s!=spawnedEnemies.end()&&s->second.native) {
                authority::MoveSample move;move.id=s->second.serverId;move.scene=feedScene;
                move.position[0]=p.x;move.position[1]=p.y;move.position[2]=p.z;
                const float qa[4]{q.x,q.y,q.z,q.w};authority::eulerDegrees(qa,move.rotation);
                if(m.samples.size()>=2) {
                    const auto& [ta,pa,qa2]=m.samples[m.samples.size()-2];const auto& [tb,pb,qb]=m.samples.back();
                    if(tb>ta) {const float dt=float(tb-ta)/1000.f;move.speed[0]=(pb.x-pa.x)/dt;move.speed[1]=(pb.y-pa.y)/dt;move.speed[2]=(pb.z-pa.z)/dt;}
                }
                const float flatSpeed=std::sqrt(move.speed[0]*move.speed[0]+move.speed[2]*move.speed[2]);
                move.state=flatSpeed>3.f?4:flatSpeed>.3f?3:1;
                if(!nativeMoveState)move.state=1;
                if(!nativeMoveSpeed)move.speed[0]=move.speed[1]=move.speed[2]=0;
                if(traceFrames>0&&s->second.serverId==traceId) {
                    // What moved the entity between our placements, and what the camera sees.
                    const auto actual=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),entity);
                    auto* component=api.call(entityModel,entity);auto* go=component?api.call(modelGo,component):nullptr;
                    V3 shown=actual;if(go&&alive(go))shown=api.value<V3>(getPosition,api.call(getTransform,go));
                    auto r3=[](float v){return std::round(v*1000.f)/1000.f;};
                    motionTrace.push_back({now-traceLastAt,r3(actual.x-m.last.x),r3(actual.z-m.last.z),r3(shown.x-actual.x),r3(shown.z-actual.z),r3(p.x-m.last.x),r3(p.z-m.last.z)});
                    traceLastAt=now;--traceFrames;
                }
                m.last=p;
                if(nativeMovePin)nativePins.emplace_back(entity,p);
                nativeMoves.push_back(move);m.applied=true;++enemyMoves;continue;
            }
            const auto actual=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),entity);
            const float dx=p.x-actual.x,dz=p.z-actual.z,dy=p.y-actual.y;
            const float flat=std::sqrt(dx*dx+dz*dz);
            auto* ctrl=api.call(gm("Entity","get_enemyCtrl","","Beyond.Gameplay.Core.EnemyController"),entity);
            auto* component=api.call(entityModel,entity);auto* go=component?api.call(modelGo,component):nullptr;
            if(!m.applied||flat>5.f||std::abs(dy)>3.f||!ctrl) {
                // First placement or far behind: snap, then walk from there.
                auto* movement=api.call(gm("Entity","get_movementComponent","","Beyond.Gameplay.Core.MovementComponent"),entity);
                if(!movement)continue;
                if(ctrl&&m.walking)api.call(gm("EnemyController","StopMove"),ctrl);
                bool warning=false;api.call(gm("MovementComponent","TeleportTo","UnityEngine.Vector3|System.Boolean"),movement,{&p,&warning});
                if(go&&alive(go))api.call(setRotation,api.call(getTransform,go),{&q});
                m.walking=false;m.applied=true;m.last=p;++enemySnaps;continue;
            }
            if(flat>.5f) {
                // Native locomotion: the enemy's own walk/run animation and turning.
                if(now-m.commandAt>=100) {
                    bool ignoreUnmovable=true;api.value<int>(gm("EnemyController","MoveTo","UnityEngine.Vector3|System.Boolean","System.Int32"),ctrl,{&p,&ignoreUnmovable});
                    m.commandAt=now;m.walking=true;++enemyWalks;
                }
            } else if(m.walking&&flat<.25f) {
                api.call(gm("EnemyController","StopMove"),ctrl);m.walking=false;
            } else if(!m.walking) {
                // Standing: face where the owner's enemy faces.
                if(go&&alive(go))api.call(setRotation,api.call(getTransform,go),{&q});
            }
            m.last=p;m.applied=true;++enemyMoves;
        }
    }
    // Host: give each live room enemy to the player it is fighting.
    void assignOwners(Net&) {
        if(!roomHost)return;
        for(auto& [key,enemy]:enemies) {
            if(owners[key]!=proto::kHostId) {owners[key]=proto::kHostId;++ownershipChanges;}
            // The host keeps its native AI. Guests have no AI execution authority.
        }
    }
    // Host: movement reports for enemies a guest simulates.

    // Guest: report enemies whose AI runs on this machine (10 Hz).

    // Claims are evidence for a native re-execution adapter, never HP deltas.
    void applyRoomHits(Session& link) {
        for(const auto& hit:link.takeHits()) {
            try {
                if(hit.at("epoch").get<std::uint64_t>()!=link.generation()||!link.canAct(hit.at("from").get<std::uint16_t>()))continue;
                const auto key=hit.at("key").get<std::string>();
                if(!link.entityMatches(key,hit.at("life").get<std::uint64_t>())||hit.at("actor")!=link.playerEntity(hit.at("from").get<std::uint16_t>()))continue;
                // No SetHpDirectly, OnHpZero, owner claim or official operation.
                // A matching native result is not implemented yet.
                lastHitClaim=hit;++hitClaimsPending;
            }catch(const std::exception& ex){replicationError=ex.what();}
        }
    }
    struct DeathObservation {void* entity=nullptr;NativeIdentity identity;std::string key;};
    // Guest in the host world: a host monster dies only when the host says so.
    // A local death (e.g. this account's own turrets) is not executed; the next
    // host snapshot restores its health.
    // Damage diagnostics: which class of target a damage hook saw.
    std::string targetClass(void* ability) {
        try {
            auto* entity=ability?api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),ability):nullptr;
            if(!entity)return "none";
            if(entity==character)return "self";
            for(const auto& [peer,actor]:actors)if(actor.entity==entity)return "actor";
            for(const auto& [key,e]:enemies)if(e.entity==entity)
                return roomEnemy(key)?(key.find(":server:")!=std::string::npos?"room_runtime":"room_level"):"tracked_other";
            if(privateEnemies.contains(entity))return "private";
            if(api.call(gm("Entity","get_enemyCtrl","","Beyond.Gameplay.Core.EnemyController"),entity))return "untracked_enemy";
            return "other";
        }catch(...) {return "error";}
    }
    void noteDamage(const char* hook,void* ability,const std::string& result) {
        const auto key=std::string(hook)+":"+targetClass(ability)+":"+result;
        if(damageCensus.size()<128||damageCensus.contains(key))++damageCensus[key];
    }
    void setFeedOption(const nlohmann::json& input) {
        if(input.contains("detach_scripts"))detachFedScripts=input.at("detach_scripts").get<bool>();
        if(input.contains("native_move"))nativeMovePuppets=input.at("native_move").get<bool>();
        if(input.contains("native_move_notify"))nativeMoveNotify=input.at("native_move_notify").get<bool>();
        if(input.contains("host_owns_all")&&!input.at("host_owns_all").get<bool>())throw std::runtime_error("客机 AI 权限已删除");
        if(input.contains("native_move_speed"))nativeMoveSpeed=input.at("native_move_speed").get<bool>();
        if(input.contains("native_move_state"))nativeMoveState=input.at("native_move_state").get<bool>();
        if(input.contains("native_move_pin"))nativeMovePin=input.at("native_move_pin").get<bool>();
        if(input.contains("trace_frames")) {
            traceFrames=(std::min)(600,input.at("trace_frames").get<int>());traceId=input.value("trace_id",std::uint64_t(0));
            motionTrace=nlohmann::json::array();traceLastAt=nowMs(); // id 0: chosen on the game thread
        }
        if(input.contains("native_move_interval_ms"))nativeMoveIntervalMs=(std::min)(std::uint64_t(500),input.at("native_move_interval_ms").get<std::uint64_t>());
    }
    bool guestBlocksDeath(Net& net,void* entity) {
        try {
            if(!filtering()||net.currentRole()!=Role::Client||!session||!entity)return false;
            const auto& hosted=session->snapshot();
            for(const auto& [key,enemy]:enemies)if(enemy.entity==entity&&currentEnemy(enemy)&&roomEnemy(key)) {
                for(const auto& e:hosted.at("entities"))if(e.at("key")==key&&e.value("alive",true)&&e.at("hp").get<double>()>0) {
                    ++guestDeathsBlocked;return true;
                }
                return false;
            }
        }catch(...) {}
        return false;
    }
    std::optional<DeathObservation> beforeNativeDeath(Net& net,void* entity) {
        try {
            if(net.currentRole()!=Role::Host||!roomHost||!entity)return {};
            for(const auto& [key,enemy]:enemies)if(enemy.entity==entity&&currentEnemy(enemy))
                return DeathObservation{entity,enemy.identity,key};
        }catch(...) {}
        return {};
    }
    void afterNativeDeath(const DeathObservation& death) {
        try {
            // Die may reject a transition. Observe the native flag after it runs;
            // never accept a client's damage number or manufacture a death.
            if(!death.identity.matches(identityOf(death.entity))||
                !api.value<bool>(gm("Entity","get_markDie","","System.Boolean"),death.entity))return;
            auto seed=hostEnemySeeds.find(death.key);if(seed==hostEnemySeeds.end())return;
            if(seed->second.value("alive",true))++nativeDeaths;
            seed->second["alive"]=false;
            if(auto state=enemyState(death.entity,death.key);!state.empty())seed->second["hp"]=state.at("hp");
        }catch(...) {}
    }
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    void queueDiagnostic(nlohmann::json command) {
        if(diagnosticQueue.size()>=8)throw std::runtime_error("诊断命令排队已满");
        diagnosticQueue.push_back(std::move(command));
    }
    void runDiagnostics() {
        while(!diagnosticQueue.empty()) {
            auto command=std::move(diagnosticQueue.front());diagnosticQueue.pop_front();
            const auto action=command.value("action",std::string{});
            nlohmann::json record={{"action",action},{"at",nowMs()},
                {"command_id",command.value("command_id",std::string{})}};
            try {
                if(action=="native_owner_probe")record["result"]=pinEnemyOwner(command);
                else if(action=="native_travel_probe") {queueSelfTravel(command);record["result"]="queued";}
                else if(action=="native_self_cast_probe")record["result"]=selfCast(command.value("automatic",false));
                else if(action=="native_feed_lifecycle_probe")record["result"]=feedLifecycleProbe(command);
                else if(action=="enemy_census")record["result"]=enemyCensus();
                else if(action=="meta_search")record["result"]=metadataIndex.search(command.at("method").get<std::string>(),command.value("class",std::string{}),(std::min)(std::size_t(400),command.value("limit",std::size_t(200))));
                else if(action=="send_traces")record["result"]=resolveSendTraces();
                else if(action=="teleport_marks")record["result"]=teleportMarks(command);
                else if(action=="class_info")record["result"]=metadataIndex.classInfo(command.at("class").get<std::string>());
                else if(action=="native_move_probe")record["result"]=nativeMoveProbe(command);
                else throw std::runtime_error("unknown diagnostic");
            }catch(const std::exception& e){record["error"]=e.what();}
            if(diagnosticResults.size()>=8)diagnosticResults.erase(diagnosticResults.begin());
            diagnosticResults.push_back(std::move(record));
        }
    }
    // Guest: move one host monster with a native SC_MOVE_OBJECT_MOVE.
    nlohmann::json nativeMoveProbe(const nlohmann::json& command) {
        if(!filtering())throw std::runtime_error("only inside the host world");
        const auto wanted=command.value("id",std::uint64_t(0));
        void* chosen=nullptr;std::uint64_t id=0;double best=1e9;
        V3 self=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
        for(const auto& [key,s]:spawnedEnemies)if(s.native)if(auto* entity=nativeEntity(s.serverId)) {
            if(wanted&&s.serverId!=wanted)continue;
            const auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),entity);
            const double d=std::hypot(p.x-self.x,p.z-self.z);if(d<best){best=d;chosen=entity;id=s.serverId;}
        }
        if(!chosen)throw std::runtime_error("no host monster here");
        const auto before=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),chosen);
        authority::MoveSample move;move.id=id;move.scene=feedScene;
        move.position[0]=before.x+command.value("dx",3.f);move.position[1]=before.y;move.position[2]=before.z+command.value("dz",0.f);
        move.speed[0]=command.value("speed",2.f);move.state=command.value("state",3);move.serverTime=command.value("server_time",0u);
        FeedScope scope(feedOperating);feed.bind(runtime);feed.move({move},command.value("server_notify",false));
        const auto after=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),chosen);
        return {{"id",id},{"before",{before.x,before.y,before.z}},{"target",{move.position[0],move.position[1],move.position[2]}},
            {"immediately_after",{after.x,after.y,after.z}},{"route",feed.moveRoute}};
    }
    // Read-only identity of every enemy the client currently has (game thread).
    nlohmann::json enemyCensus() {
        nlohmann::json out=nlohmann::json::array();
        std::set<std::uint64_t> fedNow;for(const auto& [key,s]:spawnedEnemies)if(s.native)fedNow.insert(s.serverId);
        V3 self{};if(character)self=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
        for(auto* entity:sceneEnemies()) {
            if(out.size()>=96)break;
            if(!usable(entity)||!api.call(gm("Entity","get_enemyCtrl","","Beyond.Gameplay.Core.EnemyController"),entity))continue;
            nlohmann::json e;
            const auto id=api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),entity);
            e["server_id"]=id;e["fed"]=fedNow.contains(id);
            e["battle_inst"]=api.value<std::uint64_t>(gm("Entity","get_serverBattleInstId","","System.UInt64"),entity);
            e["started"]=api.value<bool>(gm("Entity","get_hasStarted","","System.Boolean"),entity);
            e["key"]=entityKey(entity);
            e["script"]=api.value<std::uint64_t>(gm("Entity","get_parentScriptId","","System.UInt64"),entity);
            e["level_data"]=api.call(gm("Entity","get_inLevelData","","Beyond.Gameplay.LevelEntityData"),entity)!=nullptr;
            if(auto* data=api.call(gm("Entity","get_entityData","","Beyond.Gameplay.Core.BaseEntityData"),entity))
                e["template"]=text(api.call(gm("BaseEntityData","get_templateId","","System.String"),data));
            const auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),entity);
            e["distance"]=std::sqrt((p.x-self.x)*(p.x-self.x)+(p.y-self.y)*(p.y-self.y)+(p.z-self.z)*(p.z-self.z));
            e["position"]={p.x,p.y,p.z};
            if(auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),entity))
                e["hp"]=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability);
            out.push_back(std::move(e));
        }
        return out;
    }
    // Call stacks of outgoing scene/teleport requests (diagnostic, bounded).
    authority::MetadataIndex metadataIndex;
    std::deque<std::pair<std::string,std::vector<std::uintptr_t>>> sendTraces;
    void recordSendTrace(const std::string& name,std::vector<std::uintptr_t> frames) {
        if(sendTraces.size()>=24)sendTraces.pop_front();
        sendTraces.emplace_back(name,std::move(frames));
    }
    std::deque<std::string> teleportBodies; // serialized CS_SCENE_TELEPORT (base64)
    nlohmann::json resolveSendTraces() {
        nlohmann::json out=nlohmann::json::array();
        for(const auto& [name,frames]:sendTraces)out.push_back({{"message",name},{"stack",metadataIndex.resolve(frames)}});
        return {{"traces",out},{"teleport_bodies",teleportBodies}};
    }
    // Map marks of a map with activation and teleport targets (read-only).
    nlohmann::json teleportMarks(const nlohmann::json& command) {
        void* map=nullptr;
        if(command.contains("static_class")) {
            auto klass=api.klass(command.at("static_class").get<std::string>().c_str(),command.value("assembly",std::string("Gameplay.Beyond.dll")).c_str(),command.value("namespace",std::string("Beyond.Gameplay")).c_str());
            symbol<void(*)(const void*)>("il2cpp_runtime_class_init")(klass.class_info);
            auto* field=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,command.at("static_field").get<std::string>().c_str());
            if(!field)throw std::runtime_error("static field not found");
            symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(field,&map);
        } else {
            // GameInstance.instance.player.mapManager (a public field of GamePlayer)
            auto* instance=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
            auto* player=instance?api.call(gm("GameInstance","get_player","","Beyond.Gameplay.GamePlayer","Beyond.Gameplay"),instance):nullptr;
            if(player)map=readField<void*>(player,"GamePlayer","mapManager","Beyond.Gameplay");
        }
        if(!map)throw std::runtime_error("map manager unavailable");
        const auto mapName=command.value("map",mapId);const auto limit=(std::min)(std::size_t(120),command.value("limit",std::size_t(60)));
        auto* sequence=api.call(gm("MapManager","_EnumerateMarksInMap","System.String","System.Collections.Generic.IEnumerable<Beyond.Gameplay.MarkInstRuntimeData>","Beyond.Gameplay"),map,{api.string(mapName)});
        if(!sequence)throw std::runtime_error("no marks");
        eflink::unity::Api::TemporaryRoot sequenceRoot(runtime,sequence);
        auto objectClass=symbol<const void*(*)(void*)>("il2cpp_object_get_class");
        auto methodsOf=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_methods");
        auto methodName=symbol<const char*(*)(const void*)>("il2cpp_method_get_name");
        auto paramCount=symbol<unsigned(*)(const void*)>("il2cpp_method_get_param_count");
        auto find=[&](void* object,const char* suffix)->const void* {
            void* it=nullptr;const void* found=nullptr;
            while(const auto* m=methodsOf(objectClass(object),&it)) {
                const std::string n=methodName(m);
                if(paramCount(m)==0&&n.size()>=std::strlen(suffix)&&n.compare(n.size()-std::strlen(suffix),std::strlen(suffix),suffix)==0) {found=m;if(n.find("Generic")!=std::string::npos)break;}
            }
            return found;};
        auto invoke=[&](const void* method,void* self)->void* {BE_ResolvedMethodV1 r{};r.method_info=method;return api.call(r,self);};
        const auto* getEnumerator=find(sequence,"GetEnumerator");if(!getEnumerator)throw std::runtime_error("mark sequence has no enumerator");
        auto* enumerator=invoke(getEnumerator,sequence);if(!enumerator)throw std::runtime_error("mark enumerator unavailable");
        eflink::unity::Api::TemporaryRoot enumeratorRoot(runtime,enumerator);
        const auto* moveNext=find(enumerator,"MoveNext");const auto* current=find(enumerator,"get_Current");
        if(!moveNext||!current)throw std::runtime_error("mark enumerator methods unavailable");
        nlohmann::json out=nlohmann::json::array();std::size_t total=0;
        while(true) {
            auto* more=invoke(moveNext,enumerator);if(!more||!*static_cast<bool*>(runtime->object_unbox(runtime->context,more)))break;
            ++total;if(out.size()>=limit)continue;
            auto* mark=invoke(current,enumerator);if(!mark)continue;
            nlohmann::json row;row["class"]=classOf(mark);row["fields"]=metadataIndex.fields(mark,40);
            if(const auto* active=find(mark,"get_isActive"))if(auto* v=invoke(active,mark))row["active"]=*static_cast<bool*>(runtime->object_unbox(runtime->context,v));
            if(const auto* tp=find(mark,"GetTeleportPosition"))if(auto* v=invoke(tp,mark)){const auto* p=static_cast<float*>(runtime->object_unbox(runtime->context,v));row["teleport"]={p[0],p[1],p[2]};}
            out.push_back(std::move(row));
        }
        return {{"map",mapName},{"total",total},{"marks",out}};
    }
    std::set<std::uint64_t> diagnosticFeedIds;
    nlohmann::json feedLifecycleProbe(const nlohmann::json& command) {
        auto id=command.at("id").get<std::uint64_t>();
        if((id>>48)!=0x3000)throw std::runtime_error("diagnostic ID is outside the temporary monster namespace");
        const auto phase=command.at("phase").get<std::string>();
        feed.bind(runtime);
        if(phase=="leave") {
            if(!diagnosticFeedIds.contains(id))throw std::runtime_error("diagnostic does not own this ID");
            leaveNative({id});diagnosticFeedIds.erase(id);
            return {{"id",id},{"left",true}};
        }
        if(phase!="enter"||!validVector(command.at("position")))throw std::runtime_error("invalid lifecycle diagnostic");
        auto klass=api.klass("EntityManager","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
        auto* field_=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,"s_manager");
        void* manager=nullptr;if(field_)symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(field_,&manager);
        if(!manager)throw std::runtime_error("native entity manager unavailable");
        void* data=nullptr;
        if(api.value<bool>(gm("EntityManager","TryGetEntityData","System.UInt64|Beyond.Gameplay.Core.BaseEntityData&","System.Boolean"),manager,{&id,&data}))
            throw std::runtime_error("diagnostic ID is already in use");
        auto* info=staticObject("worldInfo");
        if(!info||!api.value<bool>(gm("WorldInfo","get_isStarted","","System.Boolean"),info))throw std::runtime_error("native world is not started");
        authority::FeedMonster monster;monster.id=id;monster.templateId=command.at("template").get<std::string>();
        if(monster.templateId.empty()||monster.templateId.size()>128)throw std::runtime_error("invalid native monster template");
        for(int i=0;i<3;++i)monster.position[i]=command.at("position")[i].get<float>();
        monster.scene=api.value<int>(gm("WorldInfo","get_curLevelIdNum","","System.Int32"),info);
        monster.hp=100;monster.level=1;
        // Leaving an untracked diagnostic entity reproduces private withdrawal;
        // no real private entity is removed or rebuilt for this experiment.
        if(command.value("track_enter",true))nativeOpPolicy.rememberFed(id);
        diagnosticFeedIds.insert(id);
        FeedScope scope(feedOperating);feed.enter({monster});
        return {{"id",id},{"entered",true}};
    }
    // Development only: this player's own operator casts its first normal
    // attack at the nearest room enemy, through the game's own cast path (the
    // same TryCastSkill a key press reaches). Exercises local hit resolution.
    nlohmann::json selfCast(bool automatic=false) {
        if(!character)throw std::runtime_error("当前角色未就绪");
        auto* caster=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),character);
        if(!caster)throw std::runtime_error("角色能力组件未就绪");
        if(automatic) {
            auto* battle=staticObject("battle");
            auto* target=battle?api.call(gm("BattleManager","get_mainTarget","","Beyond.Gameplay.Core.AbilitySystem"),battle):nullptr;
            auto* entity=target?api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),target):nullptr;
            std::string key;for(const auto& [id,e]:enemies)if(e.entity==entity&&currentEnemy(e)){key=id;break;}
            const bool accepted=api.value<bool>(gm("AbilitySystem","TryCastNormalAttack","","System.Boolean"),caster);
            return {{"automatic",true},{"accepted",accepted},{"target",key},{"private_target",privateEnemies.contains(entity)},
                {"casting",anySkillCasting(caster)},
                {"can_normal_attack",api.value<bool>(gm("AbilitySystem","CanCastNormalAttackAttack","","System.Boolean"),caster)},
                {"normal_attack_cd",api.value<bool>(gm("AbilitySystem","CheckNormalAttackCd","","System.Boolean"),caster)}};
        }
        const auto here=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
        std::string key;void* targetEntity=nullptr;double best=1e18;
        for(const auto& [id,e]:enemies) {
            if(!currentEnemy(e)||!roomEnemy(id)||!api.value<bool>(entityAlive,e.entity))continue;
            const auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),e.entity);
            const double d=double(p.x-here.x)*(p.x-here.x)+double(p.y-here.y)*(p.y-here.y)+double(p.z-here.z)*(p.z-here.z);
            if(d<best){best=d;key=id;targetEntity=e.entity;}
        }
        if(!targetEntity)throw std::runtime_error("附近没有房间敌人");
        auto* target=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),targetEntity);
        auto* attacks=api.call(gm("AbilitySystem","get_curNormalAttackList","","System.Collections.Generic.List<System.String>"),caster);
        if(!attacks)throw std::runtime_error("没有普攻列表");
        eflink::unity::Api::TemporaryRoot attacksRoot(runtime,attacks);int first=0;
        const auto id=text(collection(attacks,"get_Item",{&first}));
        void* skill=nullptr;api.value<bool>(gm("AbilitySystem","TryGetSkill","System.String|Beyond.Gameplay.Core.Skill&","System.Boolean"),caster,{api.string(id),&skill});
        if(!skill)throw std::runtime_error("普攻技能不存在");
        float limit=120.f;auto* handle=api.call(gm("Skill","GetDefaultTarget","Beyond.Gameplay.Core.AbilitySystem|System.Single","Beyond.Gameplay.Core.TargetHandle"),skill,{target,&limit});
        auto* view=runtime->object_new(runtime->context,api.klass("TargetHandleView","Gameplay.Beyond.dll","Beyond.Gameplay.Core").class_info);
        eflink::unity::Api::TemporaryRoot viewRoot(runtime,view);auto* viewRaw=runtime->object_unbox(runtime->context,view);
        if(handle) {eflink::unity::Api::TemporaryRoot handleRoot(runtime,handle);api.call(gm("TargetHandleView",".ctor","Beyond.Gameplay.Core.TargetHandle"),viewRaw,{handle});}
        auto castMethod=gm("AbilitySystem","TryCastSkill","System.String|Beyond.Gameplay.Core.TargetHandleView|System.Action<Beyond.Gameplay.Core.Skill.FinishType,System.Int32>|System.Nullable<Beyond.Gameplay.Core.Skill.CastSkillOptions>|System.Action<Beyond.Gameplay.Core.Skill>|System.Action<Beyond.Gameplay.Core.Skill>|Beyond.Gameplay.Core.Skill.SkillCastInfo&|System.Boolean","System.Boolean");
        auto* options=api.parameterValue(castMethod,3);eflink::unity::Api::TemporaryRoot optionsRoot(runtime,options);
        auto* info=runtime->object_new(runtime->context,api.klass("Skill/SkillCastInfo","Gameplay.Beyond.dll","Beyond.Gameplay.Core").class_info);
        eflink::unity::Api::TemporaryRoot infoRoot(runtime,info);bool no=false;
        const double before=api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),target);
        const bool accepted=api.value<bool>(castMethod,caster,{api.string(id),viewRaw,nullptr,runtime->object_unbox(runtime->context,options),nullptr,nullptr,runtime->object_unbox(runtime->context,info),&no});
        return {{"skill",id},{"key",key},{"distance",std::sqrt(best)},{"accepted",accepted},{"target_hp_before",before}};
    }
    // Development only: move this player within the current level (test setup).
    void queueSelfTravel(const nlohmann::json& command) {
        if(!command.contains("position")||!validVector(command.at("position")))throw std::runtime_error("位置无效");
        if(!character||mapId.empty())throw std::runtime_error("当前角色未就绪");
        auto* info=staticObject("worldInfo");if(!info)throw std::runtime_error("世界未就绪");
        const auto& p=command.at("position");
        travel=Travel{mapId,levelId,{p[0].get<float>(),p[1].get<float>(),p[2].get<float>()},nowMs()+60000,false,api.value<int>(gm("WorldInfo","get_curLevelIdNum","","System.Int32"),info)};
        travelState="queued";
    }
    // Development only: hand a room enemy to a player for 30 s (host).
    nlohmann::json pinEnemyOwner(const nlohmann::json& command) {
        const auto peer=command.at("peer").get<std::uint16_t>();std::string key=command.value("key",std::string{});
        if(key.empty()) {
            // Nearest live room enemy to that player.
            void* body=peer==localPeer?character:nullptr;
            if(auto actor=actors.find(peer);actor!=actors.end()&&currentActor(actor->second))body=actor->second.entity;
            if(!body)throw std::runtime_error("该玩家没有原生角色");
            const auto from=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),body);double best=1e18;
            for(const auto& [id,e]:enemies) {
                if(!currentEnemy(e)||!roomEnemy(id)||!api.value<bool>(entityAlive,e.entity))continue;
                const auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),e.entity);
                const double d=double(p.x-from.x)*(p.x-from.x)+double(p.y-from.y)*(p.y-from.y)+double(p.z-from.z)*(p.z-from.z);
                if(d<best){best=d;key=id;}
            }
            if(key.empty())throw std::runtime_error("附近没有房间敌人");
            ownerPins[key]={peer,nowMs()+30000};return {{"key",key},{"distance",std::sqrt(best)}};
        }
        ownerPins[key]={peer,nowMs()+30000};return {{"key",key}};
    }
    void queueNativeCast(nlohmann::json command) {
        throw std::runtime_error("统一原生执行适配器尚未验收，旧技能执行入口已停用");
        if(diagnosticCast)throw std::runtime_error("原生诊断正在排队");
        if(!command.contains("peer")||!command.at("peer").is_number_unsigned()||!command.contains("key")||
           !command.at("key").is_string()||command.at("key").get<std::string>().size()>128||command.value("skill",std::string{}).size()>128)
            throw std::runtime_error("原生技能诊断参数无效");
        diagnosticCast=std::move(command);
    }
#endif
    bool showRemote=true,showMarkers=true;
    // Called only from the game's native damage detour, under the world mutex.
    // Enemy modifiers are stopped on guests; host enemy state is the source of truth.
    bool captureDamage(Net& net,void* ability,void* modifier) {
        if(!ability||!modifier)return false;
        try {
            auto* target=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),ability);
            for(const auto& [peer,actor]:actors)if(actor.entity==target&&currentActor(actor))return true;
            if(net.currentRole()!=Role::Client||!guestRealm)return false;
            // No guest may execute damage on a shared view or report a local
            // result. Native intent capture/execution is a separate adapter.
            return api.value<bool>(gm("AbilitySystem/Modifier","get_isDamage","","System.Boolean"),modifier);
        }catch(...) {return net.currentRole()==Role::Client&&guestRealm;}
    }
    // Borrow cursor state only on a menu transition. Never force it every frame;
    // never restore stale state over a change made by the game's own UI.
    void menu(const BE_HostApiV1* host,bool visible) {
        api.bind(host);runtime=host;
        try {
            if(visible&&!menuCursor) {
                savedLock=api.value<int>(m("Cursor","get_lockState","","UnityEngine.CursorLockMode"));
                savedVisible=api.value<bool>(m("Cursor","get_visible","","System.Boolean"));menuCursor=true;
                int unlocked=0;bool show=true;api.call(m("Cursor","set_lockState","UnityEngine.CursorLockMode","System.Void"),nullptr,{&unlocked});api.call(m("Cursor","set_visible","System.Boolean","System.Void"),nullptr,{&show});
            } else if(!visible&&menuCursor) {
                DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
                if(foregroundPid==GetCurrentProcessId()&&api.value<int>(m("Cursor","get_lockState","","UnityEngine.CursorLockMode"))==0&&
                    api.value<bool>(m("Cursor","get_visible","","System.Boolean"))) {
                    api.call(m("Cursor","set_lockState","UnityEngine.CursorLockMode","System.Void"),nullptr,{&savedLock});
                    api.call(m("Cursor","set_visible","System.Boolean","System.Void"),nullptr,{&savedVisible});
                }
                menuCursor=false;
            }
        } catch(const std::exception& e) {localError=e.what();}
    }
    // Called on the game thread every frame.
    void tick(const BE_HostApiV1* host,Net& net,Session& link) {
        runtime=host;api.bind(host);assembly=GetModuleHandleW(L"GameAssembly.dll");
        const auto frameStart=std::chrono::steady_clock::now();
        try {
            resolve();
            link.setAdapterVersion(text(api.call(m("Application","get_version","","System.String"))));
            const auto now=nowMs();
            // Bones are only read while in a room; offline the module costs nothing per frame.
            if((net.currentRole()!=Role::Offline||travel||restorationPending)&&now-lastSample>=33) {lastSample=now;sample(net);}
            auto* level=staticObject("levelLoader");auto* info=staticObject("worldInfo");
            auto* instance=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
            const bool initialized=level&&info&&instance&&
                !api.value<bool>(gm("GameLevelLoader","get_isLoading","","System.Boolean"),level)&&
                api.value<bool>(gm("WorldInfo","get_isStarted","","System.Boolean"),info)&&
                !readField<bool>(instance,"GameInstance","m_isModeDelayedToSwitch","Beyond.Gameplay");
            if(nativeSceneInitialized&&!initialized)link.invalidateNativeScene();
            nativeSceneInitialized=initialized;
            const bool sceneReady=initialized&&(!travel||!travel->started)&&character&&api.call(mainCharacter)==character&&
                text(api.call(mapIdText,info))==mapId&&text(api.call(levelIdText,info))==levelId;
            link.setLocalSceneReady(sceneReady);
            if(initialized&&!travel&&nativeTravelParameterRoot) {release(nativeTravelParameterRoot);traceNative("scene.load_parameter.released");}
            localError.clear();
        } catch(const std::exception& e) {link.setLocalSceneReady(false);localError=e.what();character=nullptr;if(model) {try {dropLocalModel();} catch(...) {}}}
#ifdef EFLINK_NATIVE_DIAGNOSTICS
        runDiagnostics(); // lifecycle diagnostics must also execute while offline
#endif
        if(nowMs()-lastReplication>=50) {
            lastReplication=nowMs();
            const auto started=std::chrono::steady_clock::now();
            try {replication(net,link);}catch(const std::exception& e) {replicationError=e.what();}
            perf.replicationMs=elapsedMs(started);
        }
        for(auto it=observedPlayerCasts.begin();it!=observedPlayerCasts.end();) {
            bool valid=false;try {valid=net.currentRole()!=Role::Offline&&link.canObservePlayer(net.snapshot().localId)&&it->second.epoch==link.generation()&&
                it->second.actor==link.playerEntity(net.snapshot().localId)&&nowMs()-it->second.seen<30000&&currentEntity(it->second.entity,it->second.source);}catch(...) {}
            if(!valid) {release(it->second.root);it=observedPlayerCasts.erase(it);}else ++it;
        }
        nativePlayerCastBindings.retain(net.snapshot().roomId,link.playerEntity(net.snapshot().localId),link.generation(),nowMs());
        updatePresentation(net,link);
        updateResources(net,link);
        for(const auto& event:link.takePlayerCasts()) {lastPlayerCast=event;++playerCastEventsSeen;}
        if(showMarkers&&nowMs()-lastProjection>=50) {lastProjection=nowMs();try {projectPeers(net.snapshot());}catch(const std::exception& e) {replicationError=e.what();markers=nlohmann::json::array();}}
        else if(!showMarkers) markers=nlohmann::json::array();
        const auto remotesStart=std::chrono::steady_clock::now();
        try {
            if(!avatars.empty())clear(); // uncertified native views are unsupported
        } catch(const std::exception& e) {localError=e.what();}
        try {if(link.localSceneOperational()&&((net.currentRole()==Role::Host&&roomHost)||(net.currentRole()==Role::Client&&guestRealm)))updateEnemyMotion();}
        catch(const std::exception& e) {replicationError=e.what();}
        perf.remotesMs=elapsedMs(remotesStart);
        perf.frameMs=elapsedMs(frameStart);
        // Worst frame over the last two seconds.
        if(nowMs()-perf.windowStart>2000) {perf.frameMaxMs=perf.windowMax;perf.windowMax=0;perf.windowStart=nowMs();}
        perf.windowMax=(std::max)(perf.windowMax,perf.frameMs);
    }
    // Game thread only.
    void clear() {
        clearPresentation();
        clearResources();
        for(auto& [id,a]:avatars) {try {destroyAvatar(a);} catch(...) {}}
        avatars.clear();
    }
    void shutdown() {
        clearResources();
        clearPlayerCastObservations();
#ifdef EFLINK_NATIVE_DIAGNOSTICS
        if(!diagnosticFeedIds.empty())try {
            leaveNative({diagnosticFeedIds.begin(),diagnosticFeedIds.end()});diagnosticFeedIds.clear();
        }catch(...) {}
#endif
        menu(runtime,false);
        try {restoreBackground();}catch(...) {}
        clear();
        dropActors();
        const bool inHostWorld=nativeFeedAdapter&&guestRealm;
        dropEnemies();
        if((inHostWorld||restorationPending)&&!restorePrivateWorld())throw std::runtime_error("个人世界尚未恢复: "+feedError);
        travel.reset();markers=nlohmann::json::array();
        try {dropLocalModel();} catch(...) {}
        if(loader) {
            try {api.call(m("SimpleAssetLoader","Dispose","","System.Void","Common.Beyond.dll","Beyond.Resource"),loader);} catch(...) {}
            release(loaderRoot);loader=nullptr;
        }
        resetAppearance();
    }
    nlohmann::json status() const {
        nlohmann::json remote=nlohmann::json::array();
        for(const auto& [id,a]:avatars) remote.push_back({{"id",id},{"name",a.name},{"model",a.modelPath},{"spawned",a.object!=nullptr},
            {"visible",a.visible},{"native_entity",a.native},{"bones_matched",std::count_if(a.bones.begin(),a.bones.end(),[](void* b){return b!=nullptr;})},
            {"poses",a.poses},{"position",{a.entityPosition.x,a.entityPosition.y,a.entityPosition.z}},{"entity_moves",a.entityMoves},{"error",a.error}});
        nlohmann::json spawnStatus=nlohmann::json::array();for(const auto& [key,s]:spawnedEnemies) spawnStatus.push_back({{"key",key},{"submitted",s.data!=nullptr},
            {"pipeline",s.pipeline},{"resources_complete",s.resourcesComplete},{"in_aoi",s.inAoi},{"preload",s.preload},{"started",s.started},
            {"native_data_ready",s.nativeDataReady},{"awaiting_native_entity",s.native&&s.nativeDataReady&&!s.started},
            {"initial_state_applied",s.initialStateApplied},{"error",s.error}});
        nlohmann::json actorStatus=nlohmann::json::array();for(const auto& [peer,a]:actors)actorStatus.push_back({{"peer",peer},{"entity",a.id},{"template",a.templateId},
            {"registered",a.registered},{"inited",a.inited},{"started",a.started},{"waiting",a.waiting},{"bound",a.bound},
            {"hp",a.hp},{"max_hp",a.maxHp},{"skills",a.skillCount},{"casting",a.casting},{"ability_ticking",a.abilityTicking},
            {"growth",a.growth},
            {"appearance",a.appearance},{"weapon_loaded",a.weaponLoaded},{"weapon_reloads",a.weaponReloads},{"weapon_error",a.weaponError},
            {"weapon_state",a.weaponState},
            {"authorized_teleports",a.teleports},{"error",a.error}});
        nlohmann::json targetStatus=nlohmann::json::array();for(const auto& [key,e]:enemies)targetStatus.push_back({{"key",key},{"queries",e.targetQueries},{"enable_queries",e.enableQueries},{"assignments",e.targetAssignments},{"foreign_targets_rejected",e.foreignTargetsRejected},{"target_actor",e.targetActor},{"range",e.targetRange},{"error",e.targetError}});
        auto result=nlohmann::json{{"local_model",localModel},{"map_id",mapId},{"level_id",levelId},{"local_valid",character!=nullptr},{"local_hp",localHp},{"local_max_hp",localMaxHp},
            {"authority_architecture",{{"phase","legacy_removed_adapters_unverified"},{"shared_world_supported",false},{"full_sync_verified",false},
                {"guest_result_submission",false},{"guest_ai_authority",false},{"native_gameplay_replay",false},
                {"account_risk",true},{"official_safety_guaranteed",false},{"native_paths_verified",false}}},
            {"local_bones",bones.size()},{"samples",samples},{"error",localError},{"avatars",remote},{"markers",markers},
            {"appearance",{{"local",localAppearanceInfo},{"ready",character!=nullptr&&appearanceModel==localModel&&!localAppearanceInfo.is_null()},
                {"cache",officialAppearance.status()},{"sampling_error",appearanceError},{"stage",nativeWeaponLookupReady?"native_weapon_preview":"metadata_only"},
                {"lookup_ready",nativeWeaponLookupReady},{"effects_replayed",false}}},
            {"presentation",{{"local_members",sampledParty},{"local_effects",sampledEffects},{"effects_created",effectsCreated},{"effects_released",effectsReleased},{"effects_expired",effectsExpired},
                {"active_effects",visualEffects.size()},{"native_effects",visualReadback},{"native_combat",nativeCombatReadback},{"local_materials",sampledMaterials},{"native_materials",materialReadback},
                {"materials_started",materialsStarted},{"materials_stopped",materialsStopped},{"material_error",materialError},{"poise_writes",nativePoiseWrites},
                {"party_error",partySamplingError},{"effect_error",effectSamplingError},{"error",presentationError},{"native_skill_replay",false},{"full_sync_verified",false}}},
            {"player_casts",{{"events_sent",playerCastEventsSent},{"events_seen",playerCastEventsSeen},{"tracked",observedPlayerCasts.size()},
                {"last",lastPlayerCast},{"replay_enabled",false}}},
            {"resources",{{"local",nativeResourceCensus},{"scans",resourceScans},{"unadapted_native_objects",resourceUnknown},
                {"age_ms",lastResourceSuccess?nowMs()-lastResourceSuccess:0},{"error",resourceError},{"native_operations_verified",false},{"replica_recovery_verified",false},{"full_sync_verified",false}}},
            {"hit_provenance",{{"matched",nativePlayerHitMatched},{"missing",nativePlayerHitMissing},{"bindings",nativePlayerCastBindings.size()},
                {"last",lastPlayerHitSource},{"error",nativePlayerHitError},{"observation_only",true}}},
            {"world_replication",{{"native_entities",enemies.size()},{"private_enemies_quarantined",privateEnemies.size()},
                {"room_character_lookup_hits",roomCharacterLookups},
                {"guest_policy",{{"ready",guestGatesReady},{"restricted",guestRestrictionsActive},{"target_isolation_ready",privateTargetIsolationReady},
                    {"targets_detached",std::count_if(privateEnemies.begin(),privateEnemies.end(),[](const auto& item){return item.second.battleTargetDetached;})},
                    {"error",guestGateError},{"blocked",blockedGuestActions}}},
                {"room_owned_enemies",spawnedEnemies.size()},{"host_enemy_seeds",hostEnemySeeds.size()},{"matched",matchedEnemies},{"missing",missingEnemies},{"deferred_native",deferredNativeEnemies},
                {"native_feed",{{"enabled",nativeFeedAdapter},{"filter_active",feedFilterActive},{"entered",feed.entered},{"left",feed.left},
                    {"private_recorded",privateRecords.size()},{"withheld",withheldCount},{"withheld_pending",withheldOfficial.size()},
                    {"withheld_bytes",withheldOfficial.byteSize()},{"restoration_pending",restorationPending},{"restoration_attempts",restorationAttempts},
                    {"awaiting_official_resend",restoreAwaitOfficial},{"recovery_abort",recoveryAbort},
                    {"fed",std::count_if(spawnedEnemies.begin(),spawnedEnemies.end(),[](const auto& item){return item.second.native;})},
                    {"ops_suppressed",opsSuppressed},{"known_fed_owners",nativeOpPolicy.fedCount()},{"retired_owners",nativeOpPolicy.retiredCount()},
                    {"official_cached",officialMonsters.size()},{"official_forwarded",officialForwarded},{"detach_scripts",detachFedScripts},{"private_leave_skipped",privateLeaveSkipped},{"feed_waits_for_release",feedWaitsForRelease},{"scene_mode",hostModeId},{"host_owns_all",hostOwnsAll},{"in_subgame",hostInSubGame},{"native_move",nativeMovePuppets},{"native_move_speed",nativeMoveSpeed},{"native_move_pin",nativeMovePin},{"pin_errors",pinErrors},{"native_move_state",nativeMoveState},{"trace_id",traceId},{"trace_left",traceFrames},{"motion_trace",motionTrace},{"native_move_interval_ms",nativeMoveIntervalMs},{"native_moves_sent",feed.moved},{"native_move_route",feed.moveRoute},{"native_move_errors",nativeMoveErrors},{"native_move_error",nativeMoveError},{"damage_census",damageCensus},{"official_restored",officialRestored},{"unattributed_ops",[&]{nlohmann::json o=nlohmann::json::object();for(const auto& [id,n]:leakedOwners)o[std::to_string(id)]=n;return o;}()},{"errors",feedErrors},{"error",feedError}}},
                {"host_map_change",{{"pending",hostMapConsent.pending()},{"deferred",hostMapConsent.deferred()},{"token",hostMapConsent.token()},{"map",hostMapChoiceMap},{"level",hostMapChoiceLevel}}},
                {"error",replicationError},{"spawns",spawnStatus},{"players",actorStatus},{"enemy_targets",targetStatus},{"teleport",travelState},{"native_load_parameter_retained",nativeTravelParameterRoot!=0},{"official_teleport",officialTeleportStatus},{"official_teleports",officialTeleports},{"travel_lifecycle",travelLifecycle},{"teleports_completed",completedTeleports},{"damage_requests",damageRequests},
                {"combat",{{"guest_hits_sent",guestHitsSent},{"room_hits_applied",roomHitsApplied},{"player_hits_sent",playerHitsSent},
                    {"settlement_policy","host_native_only"},{"legacy_numeric_damage",false},{"native_damage_verified",false},
                    {"hit_claims_pending",hitClaimsPending},{"last_hit_claim",lastHitClaim},
                    {"damage_hook_calls",damageHookCalls},{"guest_hit_candidates",guestHitCandidates},{"guest_zero_damage",guestZeroDamage},{"guest_hit_send_failed",guestHitSendFailed},{"guest_deaths_blocked",guestDeathsBlocked},{"guest_hit_sources",guestHitSources},
                    {"player_hits_taken",playerHitsTaken},{"enemy_casts_sent",enemyCastsSent},{"enemy_casts_mirrored",enemyCastsMirrored},
                    {"enemy_casts_rejected",enemyCastsRejected},{"overworld",overworldCached},
                    {"owned_here",std::count_if(owners.begin(),owners.end(),[&](const auto& o){return o.second==localPeer;})},
                    {"owned_by_others",std::count_if(owners.begin(),owners.end(),[&](const auto& o){return o.second!=localPeer;})},
                    {"ownership_changes",ownershipChanges},{"enemy_walks",enemyWalks},{"enemy_snaps",enemySnaps},{"enemy_states_sent",enemyStatesSent},{"enemy_states_applied",enemyStatesApplied},{"enemy_moves",enemyMoves}}},
                {"native_casts",nativeCasts},{"native_cast_rejected",nativeCastRejected},{"last_native_cast",lastNativeCast},{"native_hits",nativeHits},{"native_deaths",nativeDeaths},
                {"perf",{{"frame_ms",perf.frameMs},{"frame_max_ms",perf.frameMaxMs},{"replication_ms",perf.replicationMs},
                    {"remotes_ms",perf.remotesMs},{"enemy_scan_ms",perf.scanMs},{"container_enemy_scan",perf.containerScan},{"container_error",perf.containerError}}},
                {"home",home?nlohmann::json{{"map",home->map},{"level",home->level},{"position",{home->position.x,home->position.y,home->position.z}}}:nlohmann::json()},
                {"simulation_paused",simulationPaused},{"time_scale",nativeTimeScale},{"delta_time",nativeDeltaTime},{"background_lease",backgroundLease}}}};
#ifdef EFLINK_NATIVE_DIAGNOSTICS
        result["world_replication"]["native_operations"]=nativeOperations;
        for(auto& entry:result["world_replication"]["enemy_targets"])if(auto enemy=enemies.find(entry.at("key").get<std::string>());enemy!=enemies.end())entry["ai_diagnostics"]=enemy->second.aiDiagnostics;
        result["world_replication"]["local_movement_diagnostics"]=localMovementDiagnostics;
        result["world_replication"]["diagnostics"]=diagnosticResults;
        for(auto& entry:result["world_replication"]["players"])if(auto actor=actors.find(entry.at("peer").get<std::uint16_t>());actor!=actors.end()) {
            entry["movement_diagnostics"]=actor->second.movementDiagnostics;
        }
#endif
        return result;
    }
};
}
