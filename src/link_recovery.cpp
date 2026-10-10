// Development recovery helper. No GUI, networking, input injection or room actors.
// Reads scene state and performs guarded native recovery on the logic thread.
// It never clears arbitrary forbid handles or writes game state fields.
#include "unity_bridge.h"
#include "runtime_scope.h"
#include "nlohmann/json.hpp"
#include <atomic>
#include <mutex>
using nlohmann::json;
namespace {
#ifndef EFLINK_RECOVERY_ID
#define EFLINK_RECOVERY_ID "eflink.recovery1"
#endif
constexpr char id[]=EFLINK_RECOVERY_ID;
BE_ThirdPartyHostV1 host{};
eflink::unity::Api api;
std::mutex mutex;
json snapshot={{"ready",false}};
std::atomic<bool> running=false,readRequested=true,leaveRequested=false,restoreRequested=false,recoverRequested=false;
using Tick=void(__fastcall*)(void*,float,void*);
Tick nextTick=nullptr;BE_ResolvedMethodV1 tick{};
const void* cameraClass=nullptr;
template<class T>T sym(const char* name) {
    auto p=reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),name));
    if(!p)throw std::runtime_error(std::string("Missing runtime export: ")+name);return p;
}
const BE_HostApiV1* runtime(){return host.get_runtime?host.get_runtime(host.context):host.runtime;}
BE_ResolvedMethodV1 gm(const char* type,const char* name,const char* result,const char* space="Beyond.Gameplay.Core") {
    return api.method(type,name,"",result,"Gameplay.Beyond.dll",space);
}
template<class T>T field(void* object,const char* type,const char* name,const char* space="Beyond.Gameplay.Core") {
    if(!object)throw std::runtime_error(std::string("Missing owner: ")+type);
    auto k=api.klass(type,"Gameplay.Beyond.dll",space);
    auto f=sym<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(k.class_info,name);
    if(!f)throw std::runtime_error(std::string("Missing field: ")+type+"."+name);
    T value{};sym<void(*)(void*,const void*,void*)>("il2cpp_field_get_value")(object,f,&value);return value;
}
void* world(const char* name){
    auto k=api.klass("GameWorld","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
    auto f=sym<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(k.class_info,name);
    if(!f)throw std::runtime_error(std::string("Missing world field: ")+name);
    void* value=nullptr;sym<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(f,&value);return value;
}
std::string text(void* value){
    if(!value)return {};
    const int n=sym<int(*)(void*)>("il2cpp_string_length")(value);
    if(n<0||n>4096)throw std::runtime_error("Invalid native string length");
    const auto chars=sym<const wchar_t*(*)(void*)>("il2cpp_string_chars")(value);
    std::string result(WideCharToMultiByte(CP_UTF8,0,chars,n,nullptr,0,nullptr,nullptr),'\0');
    WideCharToMultiByte(CP_UTF8,0,chars,n,result.data(),int(result.size()),nullptr,nullptr);return result;
}
std::string typeName(void* value){
    if(!value)return {};
    auto c=sym<const void*(*)(void*)>("il2cpp_object_get_class")(value);
    return std::string(sym<const char*(*)(const void*)>("il2cpp_class_get_namespace")(c))+"."+
        sym<const char*(*)(const void*)>("il2cpp_class_get_name")(c);
}
json readScene(bool leave,bool restore,bool recover=false){
    api.bind(runtime());
    auto* info=world("worldInfo");auto* loader=world("levelLoader");
    auto* dungeon=api.call(gm("GameInstance","get_dungeonManager","Beyond.Gameplay.Core.DungeonManager","Beyond.Gameplay"));
    auto* mode=api.call(gm("GameInstance","get_mode","Beyond.Gameplay.Core.BaseGameMode","Beyond.Gameplay"));
    json result={{"ready",true},{"sampled_at",GetTickCount64()},
        {"world_started",api.value<bool>(gm("WorldInfo","get_isStarted","System.Boolean"),info)},
        {"map",text(api.call(gm("WorldInfo","get_curMapIdStr","System.String"),info))},
        {"level",text(api.call(gm("WorldInfo","get_curLevelId","System.String"),info))},
        {"loading",loader&&api.value<bool>(gm("GameLevelLoader","get_isLoading","System.Boolean"),loader)},
        {"mode",typeName(mode)},
        {"mode_id",mode?text(api.call(gm("BaseGameMode","get_modeId","System.String"),mode)):std::string{}},
        {"main_scope",api.value<bool>(gm("GameInstance","get_isMainScope","System.Boolean","Beyond.Gameplay"))},
        {"dungeon",dungeon?text(api.call(gm("DungeonManager","get_curDungeonId","System.String"),dungeon)):std::string{}},
        {"in_dungeon",dungeon&&api.value<bool>(gm("DungeonManager","get_inDungeon","System.Boolean"),dungeon)}};
    auto* instance=api.call(gm("GameInstance","get_instance","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
    if(instance) {
        result["mode_switch_delayed"]=field<bool>(instance,"GameInstance","m_isModeDelayedToSwitch","Beyond.Gameplay");
        result["pending_mode"]=typeName(field<void*>(instance,"GameInstance","m_gameModeDelayToSwitch","Beyond.Gameplay"));
        result["pending_chapter_mode"]=text(field<void*>(instance,"GameInstance","m_pendingChapterModeId","Beyond.Gameplay"));
        result["has_pending_chapter_mode"]=field<bool>(instance,"GameInstance","m_hasPendingChapterMode","Beyond.Gameplay");
    }
    auto* player=api.call(gm("GameInstance","get_player","Beyond.Gameplay.GamePlayer","Beyond.Gameplay"));
    if(player) {
        auto* forbid=field<void*>(player,"GamePlayer","forbidSystem","Beyond.Gameplay");
        result["forbid_system_present"]=forbid!=nullptr;
    }
    if(auto* subgame=world("subGameManager");subgame) {
        result["in_subgame"]=api.value<bool>(gm("SubGameManager","get_inSubGame","System.Boolean"),subgame);
        result["subgame_id"]=text(api.call(gm("SubGameManager","get_topSubGameId","System.String"),subgame));
        result["subgame_instance_id"]=text(api.call(gm("SubGameManager","get_topSubGameInstId","System.String"),subgame));
        result["subgame_type"]=typeName(api.call(gm("SubGameManager","get_topSubGame","Beyond.Gameplay.Core.ISubGame"),subgame));
    }
    if(leave){
        if(!dungeon||!result["in_dungeon"].get<bool>()||result["dungeon"].get<std::string>().empty())
            result["leave_result"]="No active native dungeon; no exit was invoked";
        else if(result["loading"].get<bool>())result["leave_result"]="Native scene loading; no exit was invoked";
        else{api.call(gm("DungeonManager","LeaveDungeon","System.Void"),dungeon);result["leave_result"]="Native LeaveDungeon invoked; awaiting game response";}
    }
    if(restore||recover) {
        // Verified SwitchMode's empty-id path uses GameModeFactory.CreateMode(null)
        // and the native Deactivate/Activate sequence, including mode-owned tags.
        // Restrict this repair to the observed abandoned dungeon-mode mismatch.
        if(result["mode"]!="Beyond.Gameplay.Core.DungeonGameMode"||result["in_dungeon"].get<bool>()||
            !result["dungeon"].get<std::string>().empty()||!result["main_scope"].get<bool>()||
            result["loading"].get<bool>()||result["map"].get<std::string>().rfind("map",0)!=0)
            result["restore_result"]="Scene does not match abandoned dungeon mode; no repair invoked";
        else {
            if(!instance)throw std::runtime_error("Native game instance unavailable");
            if(recover&&result.value("in_subgame",false)) {
                // SrvLeaveSubGame checks both native IDs and performs the
                // subgame's own quit/release, countdown and stack cleanup.
                // Do not call SendQuit or fabricate an account-server reply.
                const auto game=result.value("subgame_id",std::string{});
                const auto inst=result.value("subgame_instance_id",std::string{});
                if(result.value("subgame_type",std::string{})!="Beyond.Gameplay.Core.DungeonGame"||
                    game!="dung01_bossrush01_01"||inst.empty())
                    throw std::runtime_error("Unexpected abandoned subgame; recovery preserved it");
                auto method=api.method("SubGameManager","SrvLeaveSubGame","System.String|System.String","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
                api.call(method,world("subGameManager"),{api.string(game),api.string(inst)});
                if(api.value<bool>(gm("SubGameManager","get_inSubGame","System.Boolean"),world("subGameManager")))
                    throw std::runtime_error("Native subgame exit did not finish; world start deferred");
            }
            auto method=api.method("GameInstance","SwitchMode","System.String","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay");
            api.call(method,instance,{api.string("")});
            result["restore_result"]="Native SwitchMode default path invoked; awaiting mode verification";
            if(recover&&!result["world_started"].get<bool>()) {
                // StartWorld is idempotent and runs GameInstance.OnWorldStart,
                // including the delayed mode's Deactivate/Activate sequence.
                // Never substitute a write to the isStarted backing field.
                bool inNewMap=true;
                auto start=api.method("WorldInfo","StartWorld","System.Boolean","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
                api.call(start,info,{&inNewMap});
                result["world_start_result"]="Native StartWorld invoked for the loaded overworld";
            }
        }
    }
    return result;
}
void __fastcall onTick(void* instance,float delta,void* info){
    if(nextTick)nextTick(instance,delta,info);
    if(!running||!instance||(info&&info!=tick.method_info))return;
    auto c=sym<const void*(*)(void*)>("il2cpp_object_get_class")(instance);
    bool camera=false;for(;c;c=sym<const void*(*)(const void*)>("il2cpp_class_get_parent")(c))if(c==cameraClass){camera=true;break;}
    if(!camera)return;
    const bool leave=leaveRequested.exchange(false),restore=restoreRequested.exchange(false),recover=recoverRequested.exchange(false);
    if(!readRequested.exchange(false)&&!leave&&!restore&&!recover)return;
    json result;try{result=readScene(leave,restore,recover);}catch(const std::exception& e){result={{"ready",false},{"error",e.what()}};}
    std::lock_guard lock(mutex);snapshot=std::move(result);
}
BE_Result BE_CALL initialize(const BE_ThirdPartyHostV1* input,const char*){
    if(!input||input->struct_size<offsetof(BE_ThirdPartyHostV1,hooks)+sizeof(input->hooks)||input->version!=1||!input->module_id||std::string(input->module_id)!=id||!input->reply||!input->hooks)return BE_Result_ContractMismatch;
    host={};std::memcpy(&host,input,(std::min)(std::size_t(input->struct_size),sizeof(host)));
    try{
        RuntimeThreadScope scope(true);if(!scope.ready()||!runtime())throw std::runtime_error("Runtime unavailable");
        api.bind(runtime());cameraClass=api.klass("CameraManager","Gameplay.Beyond.dll","Beyond.Gameplay.View").class_info;
        tick=api.method("CameraManager","TailLateTick","System.Single","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay.View");
        std::uint64_t handle=0;running=true;
        if(host.hooks->create(host.hooks->context,id,tick.method_pointer,reinterpret_cast<void*>(onTick),reinterpret_cast<void**>(&nextTick),&handle)!=BE_Result_Ok)
            throw std::runtime_error("Recovery logic-thread hook unavailable");
        return BE_Result_Ok;
    }catch(...){running=false;return BE_Result_NotReady;}
}
BE_Result BE_CALL configure(const char*){return BE_Result_Ok;}
BE_Result BE_CALL message(const char* request,const char* body){
    if(!running)return BE_Result_NotReady;
    if(!request||!body)return BE_Result_InvalidArgument;
    json result;BE_Result code=BE_Result_Ok;
    try{
        const auto action=json::parse(body).value("action",std::string("status"));
        if(action=="inspect_scene")readRequested=true;
        else if(action=="leave_dungeon")leaveRequested=true;
        else if(action=="restore_default_mode")restoreRequested=true;
        else if(action=="recover_abandoned_scene")recoverRequested=true;
        else if(action!="status")throw std::runtime_error("Unknown recovery action");
        std::lock_guard lock(mutex);result=snapshot;result["inspection_queued"]=readRequested.load();result["leave_queued"]=leaveRequested.load();result["restore_queued"]=restoreRequested.load();result["recovery_queued"]=recoverRequested.load();
    }catch(const std::exception& e){code=BE_Result_InvalidArgument;result={{"error",e.what()}};}
    auto value=result.dump();return host.reply(host.context,request,code,value.c_str());
}
void BE_CALL shutdown(){running=false;if(host.hooks&&host.hooks->disable_module)host.hooks->disable_module(host.hooks->context,id);host={};}
const BE_ThirdPartyModuleV1 module{sizeof(module),1,id,initialize,configure,message,shutdown};
}
BE_EXPORT const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1(){return &module;}
