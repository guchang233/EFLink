// EF Link: LAN multiplayer for Arknights: Endfield as a Better-Endfield third-party module.
// The network runs on its own thread; drawing remote players runs on the game thread through
// CameraManager.TailLateTick; the module page and other modules control it through messages
// and the exported EFLink_GetApiV1 table.
#include "link_world.h"
#include "authority/efml_lifecycle.h"
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
bool cleanupNetworkStopped=false; // protected by lifecycle
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
void cleanupOnGameThread(const BE_HostApiV1* api) {
    if(cleanupDone.load())return;
    if(!session.prepareNativeShutdown())return;
    gui.shutdownGame(api);world.shutdown();cleanupDone.store(true);
}
void __fastcall onTick(void* instance,float delta,void* info) {
    if(nextTick) nextTick(instance,delta,info);
    // Identical native bodies can be folded together; only CameraManager's tick counts.
    if(!instance||(info&&info!=tickMethod.method_info)||!isCameraManager(instance)) return;
    std::lock_guard lock(worldMutex);
    auto* api=runtime();if(!api) return;
    if(cleanupRequested.load()) {
        try {cleanupOnGameThread(api);} catch(const std::exception& e) {hookError=e.what();}
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
            cleanupOnGameThread(api);
        } else if(initialized) {if(world.takeHostMapNotice())gui.open();gui.tickGame(api);world.menu(api,gui.visible());}
    } catch(...) {}
    rendering=false;
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
    if(result==BE_Result_Ok)hooked.store(true);
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
        {"addresses",addresses},{"game",game},{"world_session",worldSession},{"room_character_lookup_ready",false},
        {"player_cast_observer_ready",false},{"damage_hook_ready",false},{"transferred_hook_ready",false},{"immune_hook_ready",false},{"death_hook_ready",false},{"enemy_target_hook_ready",false},{"enemy_find_hook_ready",false},{"skill_hook_ready",false},{"hook_ready",hooked.load()},{"official_filter_ready",false},{"op_filter_ready",false},{"op_filter_note","旧原生上报/世界替换钩子已删除"},{"outbound",{{"ready",false},{"unsupported",true}}},{"testing_invincible",false},{"invincible_blocked",0},{"hook_error",hookError},
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
    const auto text=json({{"session",session.status()},{"world",session.snapshot()},{"native",world.status().at("world_replication")},
        {"objects",session.sharedObjects()}}).dump();
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
    else if(action=="teleport_to_player") throw std::runtime_error("共享世界传送原生适配未验收");
    else if(action=="outbound_capture"||action=="emergency_home"||action=="testing_invincible"||action=="feed_option")
        throw std::runtime_error("旧玩法/回放入口已移除；统一原生适配器尚未验收");
    else if(action=="hud") {std::lock_guard lock(worldMutex);return {{"markers",world.markerSnapshot()}};}
    else if(action=="world_state") {std::lock_guard lock(worldMutex);return {{"session",session.status()},{"world",session.snapshot()},
        {"native",world.status().at("world_replication")},{"objects",session.sharedObjects()}};}
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
        session.setShutdownBarrierAvailable(eflink::worldauthority::hasShutdownBarrier(input));
        if(!net.start()) {host={};return BE_Result_Failed;}
        cleanupRequested=false;cleanupDone=false;cleanupNetworkStopped=false;
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
BE_Result BE_CALL prepareShutdown() {
    std::lock_guard lock(lifecycle);
    if(!initialized)return BE_Result_Ok;
    if(!cleanupNetworkStopped) {
        // Stop room input before the game thread begins native restoration.
        std::lock_guard lockWorld(worldMutex);
        cleanupRequested.store(true);
        net.stop();cleanupNetworkStopped=true;
    }
    return cleanupDone.load()||(!hooked.load()&&!canvasHooked.load())?BE_Result_Ok:BE_Result_NotReady;
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
        hooked=false;canvasHooked=false;
    }
    {std::lock_guard lock(registryMutex);channels.clear();listeners.clear();}
    std::lock_guard lock(lifecycle);std::lock_guard lockWorld(worldMutex);host={};
}
const eflink::worldauthority::EfmlModuleWithShutdownBarrierV1 module{
    {sizeof(module),1,kId,initialize,configure,message,shutdown},prepareShutdown};
}
BE_EXPORT const BE_ThirdPartyModuleV1* BE_CALL EFML_GetModuleV1() {return &module.base;}
BE_EXPORT const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1() {return &module.base;}
extern "C" __declspec(dllexport) const EFLink_ApiV1* EFLINK_CALL EFLink_GetApiV1() {return &publicApi;}
extern "C" __declspec(dllexport) const EFLink_ApiV2* EFLINK_CALL EFLink_GetApiV2() {return &worldApi;}
