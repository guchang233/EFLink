#pragma once
// EF Link game side. Everything here runs on the game thread (CameraManager.TailLateTick):
// it samples native world facts; gameplay requires a verified authority adapter.
#include "link_net.h"
#include "link_session.h"
#include "unity_bridge.h"
#include "authority/native_build_policy.h"
#include "authority/native_resource_state.h"
#include "authority/native_battle_state.h"
#include "authority/native_object_bindings.h"
#include "link_native_identity.h"
#include "link_map_consent.h"
#include "authority/link_metadata.h"
#include "authority/link_official_appearance.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <functional>
#include <cmath>
#include <optional>

namespace eflink {
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
        getTransform{},getPosition{},getRotation{},getLocalRotation{},
        getLocalPosition{},isAlive{},objectName{},
        childrenOf{},mapIdText{},levelIdText{};
    const void* worldInfoField=nullptr;

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

    Session* session=nullptr;
    worldauthority::NativeObjectBindings nativeObjectBindings;
    HostMapConsent hostMapConsent;
    std::string hostMapChoiceMap,hostMapChoiceLevel;
    std::string replicationError;
    std::uint64_t lastReplication=0,lastProjection=0;
    double localHp=0,localMaxHp=0;
    nlohmann::json markers=nlohmann::json::array();
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    std::deque<nlohmann::json> diagnosticQueue;
    nlohmann::json diagnosticResults=nlohmann::json::array();
#endif
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
        getLocalRotation=m("Transform","get_localRotation","","UnityEngine.Quaternion");
        getLocalPosition=m("Transform","get_localPosition","","UnityEngine.Vector3");
        isAlive=m("Object","op_Implicit","UnityEngine.Object","System.Boolean");
        objectName=m("Object","get_name","","System.String");
        childrenOf=m("GameObject","GetComponentsInChildren","System.Type|System.Boolean","UnityEngine.Component[]");
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
    std::string entityKey(void* entity) {
        auto* data=api.call(gm("Entity","get_inLevelData","","Beyond.Gameplay.LevelEntityData"),entity);
        if(data) {
            auto klass=api.klass("LevelEntityData","Gameplay.Beyond.dll","Beyond.Gameplay");
            auto* f=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,"levelLogicId");
            std::uint64_t id=0;if(f) symbol<void(*)(void*,const void*,void*)>("il2cpp_field_get_value")(data,f,&id);
            if(id) return levelId+":level:"+std::to_string(id);
        }
        return {}; // Runtime/script identities require a verified common binding adapter.
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
public:
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    void setNativeOperationLogger(std::function<void(const std::string&)> logger) {nativeOperationLogger=std::move(logger);}
#endif
private:
    void replication(Net& net,Session& link) {
        session=&link;link.update(net);
        // Transport and read-only discovery remain available. No replica world,
        // native enemy creation, account-world withdrawal, shared teleport,
        // actor skill execution or local result projection is admitted here.
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
    #include "link_world_squad.h"
    #include "link_world_presentation.h"
    #include "link_world_resources.h"
    #include "link_world_census.h"
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

public:
    nlohmann::json inspectNativeInteractiveAssociation(const BE_HostApiV1* host,void* message,void* resource) {
        runtime=host;api.bind(host);assembly=GetModuleHandleW(L"GameAssembly.dll");resolve();
        if(!message||classOf(message)!="Proto.SC_SCENE_UPDATE_INTERACTIVE_PROPERTY"||!resource||
           classOf(resource)!="Beyond.Gameplay.Core.InteractiveDoodadCommonComponent")throw std::runtime_error("unverified native property callback association");
        authority::MetadataIndex metadata;auto* boxedId=metadata.fieldObject(message,"id_","System.UInt64");std::uint64_t id=0;
        if(!boxedId)throw std::runtime_error("native message identity unavailable");
        std::memcpy(&id,runtime->object_unbox(runtime->context,boxedId),sizeof id);
        auto* entity=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),resource);
        if(!usable(entity))throw std::runtime_error("native property callback target already released");
        auto* data=api.call(gm("Entity","get_inLevelData","","Beyond.Gameplay.LevelEntityData"),entity);std::uint64_t logic=0;
        if(data) {auto* value=metadata.fieldObject(data,"levelLogicId","System.UInt64");if(value)std::memcpy(&logic,runtime->object_unbox(runtime->context,value),sizeof logic);}
        return {{"handler_active",true},{"message_matches_level_logic",id&&logic==id},
            {"message_matches_entity_id",id&&api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),entity)==id},
            {"message_matches_nature_id",id&&api.value<std::uint64_t>(gm("InteractiveDoodadCommonComponent","get_natureResourceId","","System.UInt64"),resource)==id},
            {"gameplay_modified",false}};
    }
    nlohmann::json inspectNativeInteractiveMessage(const BE_HostApiV1* host,void* message) {
        runtime=host;api.bind(host);assembly=GetModuleHandleW(L"GameAssembly.dll");resolve();
        if(!message||classOf(message)!="Proto.SC_SCENE_UPDATE_INTERACTIVE_PROPERTY")throw std::runtime_error("unverified native interactive message class");
        authority::MetadataIndex metadata;
        auto* boxedId=metadata.fieldObject(message,"id_","System.UInt64");std::uint64_t id=0;
        if(!boxedId)throw std::runtime_error("native interactive message identity unavailable");
        std::memcpy(&id,runtime->object_unbox(runtime->context,boxedId),sizeof id);
        auto* boxedFlag=metadata.fieldObject(message,"clientOperate_","System.Boolean");bool client=false;
        if(boxedFlag)std::memcpy(&client,runtime->object_unbox(runtime->context,boxedFlag),sizeof client);
        auto* properties=metadata.fieldObject(message,"properties_","Google.Protobuf.Collections.MapField<System.Int32,Proto.DYNAMIC_PARAMETER>");
        auto* boxedCount=properties?collection(properties,"get_Count"):nullptr;int count=0;
        if(boxedCount)std::memcpy(&count,runtime->object_unbox(runtime->context,boxedCount),sizeof count);
        if(count<0||count>4096)throw std::runtime_error("native interactive message outside diagnostic bounds");
        nlohmann::json result={{"property_count",count},{"client_operate",client},{"resource_known",false},{"gameplay_modified",false}};
        auto* manager=staticObject("natureResourceManager");
        auto* dictionary=manager?readField<void*>(manager,"NatureResourceManager","m_resources","Beyond.Gameplay"):nullptr;
        if(!dictionary||!id)return result;
        auto* found=collection(dictionary,"ContainsKey",{&id});bool present=false;
        if(found)std::memcpy(&present,runtime->object_unbox(runtime->context,found),sizeof present);
        void* resource=present?collection(dictionary,"get_Item",{&id}):nullptr;
        if(!resource||classOf(resource)!="Beyond.Gameplay.Core.InteractiveDoodadCommonComponent")return result;
        const auto kind=api.value<int>(gm("InteractiveDoodadCommonComponent","get_natureResourceType","","Beyond.Gameplay.ENatureType"),resource);
        if(kind!=enumValue<int>("ENatureType","Mine","Beyond.Gameplay"))return result;
        auto* info=staticObject("worldInfo");if(!info)return result;
        mapId=text(api.call(mapIdText,info));levelId=text(api.call(levelIdText,info));
        auto* entity=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),resource);
        const auto key=usable(entity)?entityKey(entity):std::string{};if(key.empty())return result;
        result["resource_known"]=true;result["key"]=key+"/mine";
        result["map"]=mapId;result["level"]=levelId;
        result["property_hp_ratio"]=readField<int>(resource,"InteractiveDoodadCommonComponent","m_hpRatio");
        result["runtime_hp_ratio"]=readField<int>(resource,"InteractiveDoodadCommonComponent","m_hpRatioRuntime");
        result["collected"]=readField<bool>(resource,"InteractiveDoodadCommonComponent","m_isCollected");
        return result; // no message dump, unknown fields, native IDs or credentials
    }
    nlohmann::json inspectNativeResourceOperation(const BE_HostApiV1* host,void* operation,void* argument) {
        runtime=host;api.bind(host);assembly=GetModuleHandleW(L"GameAssembly.dll");resolve();
        nlohmann::json result={{"operation_present",operation!=nullptr},{"mineral",false},{"argument_matches_operator",false}};
        if(!operation)return result;
        if(classOf(operation)!="Beyond.Gameplay.Operation")throw std::runtime_error("unverified native resource operation class");
        authority::MetadataIndex metadata;
        auto scalar=[&]<class T>(const char* name,const char* type) {
            auto* boxed=metadata.fieldObject(operation,name,type);T value{};
            if(!boxed)throw std::runtime_error("native resource operation field unavailable");
            std::memcpy(&value,runtime->object_unbox(runtime->context,boxed),sizeof value);return value;
        };
        const auto operatorId=scalar.template operator()<std::uint64_t>("operatorId","System.UInt64");
        const auto resourceId=scalar.template operator()<std::uint64_t>("resourceId","System.UInt64");
        result["operation_type"]=scalar.template operator()<int>("operationType","Beyond.Gameplay.ENatureOperation");
        result["argument_matches_operator"]=usable(argument)&&api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),argument)==operatorId;
        auto* manager=staticObject("natureResourceManager");
        auto* dictionary=manager?readField<void*>(manager,"NatureResourceManager","m_resources","Beyond.Gameplay"):nullptr;
        if(!dictionary)return result;
        auto* found=collection(dictionary,"ContainsKey",{const_cast<std::uint64_t*>(&resourceId)});bool present=false;
        if(found)std::memcpy(&present,runtime->object_unbox(runtime->context,found),sizeof present);
        if(!present)return result;
        auto* resource=collection(dictionary,"get_Item",{const_cast<std::uint64_t*>(&resourceId)});
        if(!resource||classOf(resource)!="Beyond.Gameplay.Core.InteractiveDoodadCommonComponent")return result;
        const auto kind=api.value<int>(gm("InteractiveDoodadCommonComponent","get_natureResourceType","","Beyond.Gameplay.ENatureType"),resource);
        result["mineral"]=kind==enumValue<int>("ENatureType","Mine","Beyond.Gameplay");
        if(result["mineral"]==true) {
            auto* info=staticObject("worldInfo");if(!info)return result;
            levelId=text(api.call(levelIdText,info));
            auto* entity=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),resource);
            const auto key=usable(entity)?entityKey(entity):std::string{};
            if(!key.empty())result["key"]=key+"/mine";
            result["collected"]=readField<bool>(resource,"InteractiveDoodadCommonComponent","m_isCollected");
        }
        return result;
    }
    // Only local native references and asset metadata are examined. Public
    // diagnostics expose comparisons, never raw pointers or account/entity IDs.
    nlohmann::json inspectNativeAttackOrigin(void* board,void* target) {
        nlohmann::json out={{"source_reference_valid",false},{"receiver_matches_target",false},
            {"sender_matches_source",false},{"party_slot",-1},{"source_is_controlled",false},
            {"cast_present",false},{"cast_owner_matches_source",false},{"cast_instance_present",false}};
        if(!board||!target)return out;
        auto* dictionary=readField<void*>(board,"ParamBlackboard","variableDict","Beyond.Gameplay");
        if(!dictionary)return out;
        authority::MetadataIndex metadata;
        auto variable=[&](const char* name,const char* valueType)->void* {
            auto* has=collection(dictionary,"ContainsKey",{api.string(name)});bool present=false;
            if(has)std::memcpy(&present,runtime->object_unbox(runtime->context,has),sizeof present);
            if(!present)return nullptr;
            auto* value=collection(dictionary,"get_Item",{api.string(name)});
            if(!value||metadata.parameterType(value).at("value_type")!=valueType)
                throw std::runtime_error("unverified native attack parameter type");
            return value;
        };
        struct Reference {void* entity=nullptr;std::uint32_t uid=0;};
        auto entityReference=[&](const char* name)->Reference {
            auto* value=variable(name,"Beyond.Gameplay.Core.EntityPtr");if(!value)return {};
            auto* ptr=metadata.fieldObject(value,"m_value","Beyond.Gameplay.Core.EntityPtr");
            eflink::unity::Api::TemporaryRoot ptrRoot(runtime,ptr);
            auto* objectPtr=metadata.fieldObject(ptr,"<objectPtr>k__BackingField","Beyond.ObjectPtr<Beyond.Gameplay.Core.Entity>");
            eflink::unity::Api::TemporaryRoot objectRoot(runtime,objectPtr);
            auto* entity=metadata.fieldObject(objectPtr,"obj","Beyond.Gameplay.Core.Entity");
            auto* cached=metadata.fieldObject(objectPtr,"cachedUid","System.UInt32");
            std::uint32_t uid=0;if(cached)std::memcpy(&uid,runtime->object_unbox(runtime->context,cached),sizeof uid);
            if(!uid||!usable(entity)||api.value<std::uint32_t>(gm("Entity","get_instanceUid","","System.UInt32"),entity)!=uid)return {};
            return {entity,uid};
        };
        const auto source=entityReference("instigator"),sender=entityReference("@event_sender"),receiver=entityReference("@event_receiver");
        out["source_reference_valid"]=source.entity!=nullptr;
        out["sender_matches_source"]=source.entity&&source.entity==sender.entity&&source.uid==sender.uid;
        out["receiver_matches_target"]=receiver.entity&&receiver.entity==target;
        if(!source.entity)return out;
        auto* instance=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
        auto* player=instance?api.call(gm("GameInstance","get_player","","Beyond.Gameplay.GamePlayer","Beyond.Gameplay"),instance):nullptr;
        auto* squad=player?readField<void*>(player,"GamePlayer","squadManager","Beyond.Gameplay"):nullptr;
        if(squad) {
            const int count=api.value<int>(gm("SquadManager","get_slotCount","","System.Int32"),squad);
            if(count<0||count>4)throw std::runtime_error("unverified native source squad size");
            for(int slot=0;slot<count;++slot)if(api.call(gm("SquadManager","GetMemberBySlot","System.Int32","Beyond.Gameplay.Core.Entity"),squad,{&slot})==source.entity) {out["party_slot"]=slot;break;}
        }
        out["source_is_controlled"]=api.call(mainCharacter)==source.entity;
        auto* skillVariable=variable("skill_id","System.String");
        const auto skill=skillVariable?text(metadata.fieldObject(skillVariable,"m_value","System.String")):std::string{};
        if(skill.empty()||skill.size()>128)return out;
        auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),source.entity);void* nativeSkill=nullptr;
        if(!ability||!api.value<bool>(gm("AbilitySystem","TryGetSkill","System.String|Beyond.Gameplay.Core.Skill&","System.Boolean"),ability,{api.string(skill),&nativeSkill})||!nativeSkill)return out;
        eflink::unity::Api::TemporaryRoot skillRoot(runtime,nativeSkill);
        out["cast_present"]=true;
        out["cast_owner_matches_source"]=api.call(gm("Skill","get_owner","","Beyond.Gameplay.Core.AbilitySystem"),nativeSkill)==ability;
        out["cast_instance_present"]=api.value<std::uint32_t>(gm("Skill","get_skillCastId","","System.UInt32"),nativeSkill)!=0;
        return out;
    }
    // Read-only callback diagnostic. No native operation is invoked, no state
    // is applied, and parameter VALUES/actor account IDs are deliberately omitted.
    nlohmann::json inspectNativeMineralAttack(const BE_HostApiV1* host,void* object,void* board) {
        runtime=host;api.bind(host);assembly=GetModuleHandleW(L"GameAssembly.dll");resolve();
        if(!object||classOf(object)!="Beyond.Gameplay.Core.InteractiveDoodadCommonComponent")throw std::runtime_error("unexpected mineral callback class");
        auto* info=staticObject("worldInfo");if(!info)throw std::runtime_error("mineral callback world unavailable");
        mapId=text(api.call(mapIdText,info));levelId=text(api.call(levelIdText,info));
        auto* entity=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),object);
        const auto key=usable(entity)?entityKey(entity):std::string{};
        if(key.empty())throw std::runtime_error("mineral callback has no verified stable key");
        const auto kind=api.value<int>(gm("InteractiveDoodadCommonComponent","get_natureResourceType","","Beyond.Gameplay.ENatureType"),object);
        if(kind!=enumValue<int>("ENatureType","Mine","Beyond.Gameplay"))throw std::runtime_error("callback does not target a mineral");
        auto parameters=nlohmann::json::object(),parameterBoards=nlohmann::json::array();std::set<void*> seenBoards;
        const auto boardClass=board?classOf(board):std::string{};
        std::function<void(void*,const std::string&,unsigned)> inspectBoard;
        inspectBoard=[&](void* current,const std::string& path,unsigned depth) {
            if(!current||depth>3||seenBoards.size()>=16||!seenBoards.insert(current).second)return;
            auto* type=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(current);
            auto base=api.klass("ParamBlackboard","Gameplay.Beyond.dll","Beyond.Gameplay");bool compatible=false;
            for(auto* c=type;c;c=symbol<const void*(*)(const void*)>("il2cpp_class_get_parent")(c))if(c==base.class_info){compatible=true;break;}
            if(!compatible)return;
            auto* dictionary=readField<void*>(current,"ParamBlackboard","variableDict","Beyond.Gameplay");
            auto* references=readField<void*>(current,"ParamBlackboard","subBlackboardRefDict","Beyond.Gameplay");
            auto count=[&](void* map) {int value=0;if(map)if(auto* boxed=collection(map,"get_Count"))std::memcpy(&value,runtime->object_unbox(runtime->context,boxed),sizeof value);
                if(value<0||value>4096)throw std::runtime_error("mineral event parameter map outside bounds");return value;};
            parameterBoards.push_back({{"path",path},{"class",classOf(current)},{"variables",count(dictionary)},{"references",count(references)}});
            auto each=[&](void* map,const std::function<void(const std::string&,void*)>& visit) {
                if(!map)return;
                auto* keys=collection(map,"get_Keys");eflink::unity::Api::TemporaryRoot keysRoot(runtime,keys);
                auto* enumerator=genericValueCall(keys,"GetEnumerator");eflink::unity::Api::TemporaryRoot enumRoot(runtime,enumerator);
                for(unsigned i=0;i<64&&parameters.size()<128;++i) {
                    auto* box=genericValueCall(enumerator,"MoveNext");bool more=false;
                    if(!box)throw std::runtime_error("mineral event parameter enumeration unavailable");
                    std::memcpy(&more,runtime->object_unbox(runtime->context,box),sizeof more);if(!more)break;
                    const auto name=text(genericValueCall(enumerator,"get_Current"));if(name.empty()||name.size()>128)continue;
                    auto* value=collection(map,"get_Item",{api.string(name)});if(value)visit(name,value);
                }
            };
            each(dictionary,[&](const std::string& name,void* variable){parameters[path+"/"+name]=authority::MetadataIndex{}.parameterType(variable);});
            each(references,[&](const std::string& name,void* child){inspectBoard(child,path+"/"+name,depth+1);});
        };
        inspectBoard(board,"root",0);
        nlohmann::json origin;
        try {origin=inspectNativeAttackOrigin(board,entity);}catch(const std::exception& e) {origin={{"error",e.what()},{"source_reference_valid",false}};}
        return {{"map",mapId},{"level",levelId},{"key",key+"/mine"},
            {"hp_ratio",readField<int>(object,"InteractiveDoodadCommonComponent","m_hpRatioRuntime")},
            {"server_hp_ratio",readField<int>(object,"InteractiveDoodadCommonComponent","m_hpRatio")},
            {"collected",readField<bool>(object,"InteractiveDoodadCommonComponent","m_isCollected")},
            {"board_class",boardClass},{"parameter_boards",parameterBoards},{"parameter_types",parameters},{"origin",origin},{"gameplay_modified",false}};
    }
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
        try {
            auto staged=nativeObjectBindings.preview();NativeResourceSample sample(*this);sampleResources(staged,sample);
            resources.swap(sample.census);nativeResources.swap(sample.resources);properties=resourceProperties();
        }
        catch(...) {clearResources();throw;}
        auto types=resourceNativeTypes;auto unknown=resourceUnknown;clearResources();
        return {{"map",mapId},{"level",levelId},{"resources",resources},{"properties",properties},{"native_types",types},{"unadapted",unknown},{"gameplay_modified",false}};
    }
    const nlohmann::json& markerSnapshot() const {return markers;}
    bool takeHostMapNotice() {return hostMapConsent.takeNotice();}
    void chooseHostMap(Net& net,std::uint64_t token,bool follow) {
        if(net.currentRole()!=Role::Client||!session||!session->followHost)throw std::runtime_error("当前不是跟随房主的房客");
        if(follow) {
            throw std::runtime_error("原生换图隔离与恢复适配器尚未实现；跟随未支持");
        }
        hostMapConsent.choose(token,false);
    }
    void resetAppearance() {
        officialAppearance.clear();localAppearanceInfo=nullptr;lastAppearanceSample=0;appearanceModel.clear();appearanceError.clear();
    }
    void observeAppearance(const std::string& type,const std::string& bytes) {
        officialAppearance.observe(type,bytes);localAppearanceInfo=nullptr;lastAppearanceSample=0;
    }
    void rejectAppearance() {
        officialAppearance.reject("official appearance serialization failed");localAppearanceInfo=nullptr;lastAppearanceSample=0;
    }
    void restoreBackground() {
        if(!backgroundLease)return;
        if(api.value<bool>(m("Application","get_runInBackground","","System.Boolean")))api.call(m("Application","set_runInBackground","System.Boolean","System.Void"),nullptr,{&backgroundOriginal});
        backgroundLease=false;
    }
#ifdef EFLINK_NATIVE_DIAGNOSTICS
    void queueDiagnostic(nlohmann::json command) {
        const auto action=command.value("action",std::string{});
        if(action=="native_owner_probe"||action=="native_travel_probe"||action=="native_self_cast_probe"||
           action=="native_feed_lifecycle_probe"||action=="native_move_probe")
            throw std::runtime_error("未验收的原生玩法修改入口已停用；只读诊断仍可使用");
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
                if(action=="enemy_census")record["result"]=enemyCensus();
                else if(action=="meta_search")record["result"]=metadataIndex.search(command.at("method").get<std::string>(),command.value("class",std::string{}),(std::min)(std::size_t(400),command.value("limit",std::size_t(200))));
                else if(action=="send_traces")record["result"]=resolveSendTraces();
                else if(action=="teleport_marks")record["result"]=teleportMarks(command);
                else if(action=="class_info")record["result"]=metadataIndex.classInfo(command.at("class").get<std::string>());
                else throw std::runtime_error("unknown diagnostic");
            }catch(const std::exception& e){record["error"]=e.what();}
            if(diagnosticResults.size()>=8)diagnosticResults.erase(diagnosticResults.begin());
            diagnosticResults.push_back(std::move(record));
        }
    }
    // Guest: move one host monster with a native SC_MOVE_OBJECT_MOVE.
    // Read-only identity of every enemy the client currently has (game thread).
    nlohmann::json enemyCensus() {
        nlohmann::json out=nlohmann::json::array();
        V3 self{};if(character)self=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
        for(auto* entity:sceneEnemies()) {
            if(out.size()>=96)break;
            if(!usable(entity)||!api.call(gm("Entity","get_enemyCtrl","","Beyond.Gameplay.Core.EnemyController"),entity))continue;
            nlohmann::json e;
            const auto id=api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),entity);
            e["server_id"]=id;e["fed"]=false;
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
        if(command.contains("static_class")||command.contains("static_field")||command.contains("assembly")||command.contains("namespace"))
            throw std::runtime_error("diagnostics cannot select arbitrary native objects or static fields");
        {
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
    // Retired compatibility diagnostic; it cannot execute a game skill.
    void queueNativeCast(nlohmann::json) {throw std::runtime_error("统一原生执行适配器尚未验收，旧技能执行入口已删除");}
#endif
    bool showRemote=true,showMarkers=true;
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
            if((net.currentRole()!=Role::Offline)&&now-lastSample>=33) {lastSample=now;sample(net);}
            auto* level=staticObject("levelLoader");auto* info=staticObject("worldInfo");
            auto* instance=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
            const bool initialized=level&&info&&instance&&
                !api.value<bool>(gm("GameLevelLoader","get_isLoading","","System.Boolean"),level)&&
                api.value<bool>(gm("WorldInfo","get_isStarted","","System.Boolean"),info)&&
                !readField<bool>(instance,"GameInstance","m_isModeDelayedToSwitch","Beyond.Gameplay");
            if(nativeSceneInitialized&&!initialized)link.invalidateNativeScene();
            nativeSceneInitialized=initialized;
            const bool sceneReady=initialized&&character&&api.call(mainCharacter)==character&&
                text(api.call(mapIdText,info))==mapId&&text(api.call(levelIdText,info))==levelId;
            link.setLocalSceneReady(sceneReady);
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
        updatePresentation(net,link);
        updateResources(net,link);
        if(showMarkers&&nowMs()-lastProjection>=50) {lastProjection=nowMs();try {projectPeers(net.snapshot());}catch(const std::exception& e) {replicationError=e.what();markers=nlohmann::json::array();}}
        else if(!showMarkers) markers=nlohmann::json::array();
        perf.remotesMs=0; // no uncertified model/effect replay
        perf.frameMs=elapsedMs(frameStart);
        // Worst frame over the last two seconds.
        if(nowMs()-perf.windowStart>2000) {perf.frameMaxMs=perf.windowMax;perf.windowMax=0;perf.windowStart=nowMs();}
        perf.windowMax=(std::max)(perf.windowMax,perf.frameMs);
    }
    // Game thread only.
    void clear() {
        clearPresentation();
        clearResources();
        clearNativeCensus();
    }
    void shutdown() {
        clearResources();
        menu(runtime,false);
        try {restoreBackground();}catch(...) {}
        clear();
        markers=nlohmann::json::array();
        try {dropLocalModel();} catch(...) {}
        resetAppearance();
    }
    nlohmann::json status() const {
        auto result=nlohmann::json{{"local_model",localModel},{"map_id",mapId},{"level_id",levelId},
            {"local_valid",character!=nullptr},{"local_hp",localHp},{"local_max_hp",localMaxHp},
            {"authority_architecture",{{"phase","native_adapters_in_progress"},{"shared_world_supported",false},
                {"full_sync_verified",false},{"guest_result_submission",false},{"guest_ai_authority",false},
                {"native_gameplay_replay",false},{"account_risk",true},{"official_safety_guaranteed",false},{"native_paths_verified",false}}},
            {"local_bones",bones.size()},{"samples",samples},{"error",localError},{"avatars",nlohmann::json::array()},{"markers",markers},
            {"appearance",{{"local",localAppearanceInfo},{"ready",character!=nullptr&&appearanceModel==localModel&&!localAppearanceInfo.is_null()},
                {"cache",officialAppearance.status()},{"sampling_error",appearanceError},{"stage","metadata_only"},{"lookup_ready",false},{"effects_replayed",false}}},
            {"presentation",{{"local_members",sampledParty},{"local_effects",sampledEffects},{"active_effects",0},
                {"native_combat",nativeCombatReadback},{"local_materials",sampledMaterials},{"native_materials",materialReadback},
                {"party_error",partySamplingError},{"effect_error",effectSamplingError},{"material_error",materialError},
                {"error",presentationError},{"native_skill_replay",false},{"full_sync_verified",false}}},
            {"resources",{{"local",nativeResourceCensus},{"scans",resourceScans},{"unadapted_native_objects",resourceUnknown},
                {"age_ms",lastResourceSuccess?nowMs()-lastResourceSuccess:0},{"error",resourceError},
                {"native_operations_verified",false},{"replica_recovery_verified",false},{"full_sync_verified",false}}},
            {"world_replication",{{"native_bindings",nativeObjectBindings.size()},
                {"native_census",nativeCensusStatus()},
                {"native_squad_source",nativeSquadSource},
                {"native_entities",0},{"private_enemies_quarantined",0},{"room_owned_enemies",0},{"matched",0},{"missing",0},
                {"guest_policy",{{"ready",false},{"restricted",false},{"target_isolation_ready",false},{"supported",false}}},
                {"native_feed",{{"enabled",false},{"filter_active",false},{"legacy_removed",true},{"supported",false}}},
                {"host_map_change",{{"pending",hostMapConsent.pending()},{"deferred",hostMapConsent.deferred()},
                    {"token",hostMapConsent.token()},{"map",hostMapChoiceMap},{"level",hostMapChoiceLevel}}},
                {"error",replicationError},{"spawns",nlohmann::json::array()},{"players",nlohmann::json::array()},
                {"enemy_targets",nlohmann::json::array()},{"teleport","unsupported"},{"scene_travel_supported",false},{"native_load_parameter_retained",false},
                {"combat",{{"settlement_policy","host_native_only"},{"legacy_numeric_damage",false},{"native_damage_verified",false},{"supported",false}}},
                {"perf",{{"frame_ms",perf.frameMs},{"frame_max_ms",perf.frameMaxMs},{"replication_ms",perf.replicationMs},
                    {"remotes_ms",perf.remotesMs},{"container_enemy_scan",perf.containerScan},{"container_error",perf.containerError}}},
                {"home",nullptr},{"background_lease",backgroundLease}}}};
#ifdef EFLINK_NATIVE_DIAGNOSTICS
        result["world_replication"]["native_operations"]=nativeOperations;
        result["world_replication"]["diagnostics"]=diagnosticResults;
#endif
        return result;
    }
};
}
