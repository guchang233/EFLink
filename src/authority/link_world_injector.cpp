// Development world-feed injector. Delivers whitelisted server messages to THIS
// client's own receive path (NetBus.HandleMessage) on the game thread, exactly as
// the client's network layer does. It never sends anything to any server and
// never writes game memory directly; the game's own handlers apply each message.
// Purpose: prove the native feed a shared host world needs (entity lifecycle by
// server message, scene entry) before any production adapter relies on it.
#include "BetterEndfield/ThirdPartyModule.h"
#include "../runtime_scope.h"
#include "../unity_bridge.h"
#include "native_proto_codec.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <intrin.h>
#include <map>
#include <set>
#include <array>
#include <cctype>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using nlohmann::json;
#ifndef EFLINK_INJECTOR_ID
#define EFLINK_INJECTOR_ID "eflink.worldfeed1"
#endif
constexpr char id[]=EFLINK_INJECTOR_ID;
BE_ThirdPartyHostV1 host{};
std::recursive_mutex mutex;
eflink::unity::Api api;
bool installed=false;
// Server -> client gameplay messages only. Nothing in this list is ever sent.
constexpr std::array allowed={"SC_OBJECT_ENTER_VIEW","SC_OBJECT_LEAVE_VIEW","SC_SCENE_OBJECT_ENTER_VIEW_BEGIN",
    "SC_SCENE_OBJECT_ENTER_VIEW_END","SC_SELF_SCENE_INFO","SC_ENTER_SCENE_NOTIFY","SC_CHANGE_SCENE_BEGIN_NOTIFY",
    "SC_MOVE_OBJECT_MOVE","SC_ENTITY_PROPERTY_CHANGE"};
std::deque<json> commands;
json results=json::array();
void* bus=nullptr;std::uint32_t busRoot=0;
BE_ResolvedMethodV1 handleMethod{},tickMethod{};
using Handle=void(__fastcall*)(void*,const void*,void*);
using Send=std::uint64_t(__fastcall*)(void*,void*,void*);
Send nextSend=nullptr;BE_ResolvedMethodV1 sendMethod{};
std::uint64_t sentBattleOps=0;
std::uint64_t sentSceneLoadFinishes=0;
json sent=json::array(); // bounded capture of this client's own outbound battle ops
using Tick=void(__fastcall*)(void*,float,void*);
Handle nextHandle=nullptr;Tick nextTick=nullptr;
std::size_t headOffset=0,bodyOffset=0,packedOffset=0,timestampOffset=0;
std::uint64_t realMessages=0,injected=0,injectFailed=0,captureFailed=0;
// Bounded local capture of real world messages this client receives (development).
eflink::authority::NativeProtoCodec codec;bool codecReady=false;
json captured=json::array();
constexpr std::array capturedTypes={"SC_OBJECT_ENTER_VIEW","SC_OBJECT_LEAVE_VIEW","SC_SELF_SCENE_INFO",
    "SC_ENTER_SCENE_NOTIFY","SC_CHANGE_SCENE_BEGIN_NOTIFY","SC_SCENE_OBJECT_ENTER_VIEW_BEGIN","SC_SCENE_OBJECT_ENTER_VIEW_END"};
thread_local bool injecting=false;

const BE_HostApiV1* runtime() {return host.get_runtime?host.get_runtime(host.context):host.runtime;}
template<class T> T symbol(const char* name) {
    auto p=reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),name));
    if(!p)throw std::runtime_error(std::string("missing metadata export: ")+name);
    return p;
}
std::string className(void* object) {
    if(!object)return {};
    auto* klass=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(object);
    return symbol<const char*(*)(const void*)>("il2cpp_class_get_name")(klass);
}
std::string text(void* managed) {
    if(!managed)return {};
    std::vector<char> buffer(1024);
    const int n=runtime()->copy_managed_string(runtime()->context,managed,buffer.data(),buffer.size());
    return n<0?std::string{}:std::string(buffer.data());
}
// SC_OBJECT_LEAVE_VIEW -> ScObjectLeaveView (the client's own enum naming).
int messageId(const std::string& type) {
    std::string name;bool upper=true;
    for(char c:type) {
        if(c=='_') {upper=true;continue;}
        name+=upper?char(std::toupper(static_cast<unsigned char>(c))):char(std::tolower(static_cast<unsigned char>(c)));upper=false;
    }
    auto klass=api.klass("SCMessageID","Beyond.Network.Proto.dll","Proto");
    auto* field=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,name.c_str());
    if(!field)throw std::runtime_error("message id unknown: "+name);
    int value=0;symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(field,&value);
    if(value<=0)throw std::runtime_error("message id invalid: "+name);
    return value;
}
void* parse(const std::string& type,const std::string& base64) {
    auto* string=api.string(base64);eflink::unity::Api::TemporaryRoot stringRoot(runtime(),string);
    auto* data=api.call(api.method("ByteString","FromBase64","System.String","Google.Protobuf.ByteString","Google.Protobuf.dll","Google.Protobuf"),nullptr,{string});
    eflink::unity::Api::TemporaryRoot dataRoot(runtime(),data);
    auto messageClass=api.klass(type.c_str(),"Beyond.Network.Proto.dll","Proto");
    auto* getParser=symbol<const void*(*)(const void*,const char*,int)>("il2cpp_class_get_method_from_name")(messageClass.class_info,"get_Parser",0);
    if(!getParser)throw std::runtime_error("parser unavailable");
    BE_ResolvedMethodV1 parserMethod{};parserMethod.method_info=getParser;
    auto* parser=api.call(parserMethod);eflink::unity::Api::TemporaryRoot parserRoot(runtime(),parser);
    auto* parserClass=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(parser);
    // MessageParser<T>.ParseFrom(ByteString); overloads differ only by type, so match it.
    const void* parseFrom=nullptr;void* iterator=nullptr;
    auto methods=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_methods");
    auto name=symbol<const char*(*)(const void*)>("il2cpp_method_get_name");
    auto count=symbol<unsigned(*)(const void*)>("il2cpp_method_get_param_count");
    auto param=symbol<const void*(*)(const void*,unsigned)>("il2cpp_method_get_param");
    auto typeName=symbol<char*(*)(const void*)>("il2cpp_type_get_name");
    auto release=symbol<void(*)(void*)>("il2cpp_free");
    auto parent=symbol<const void*(*)(const void*)>("il2cpp_class_get_parent");
    for(auto* klass=parserClass;klass&&!parseFrom;klass=parent(klass)) {
        iterator=nullptr;
        while(auto* method=methods(klass,&iterator)) {
            if(std::strcmp(name(method),"ParseFrom")||count(method)!=1)continue;
            char* t=typeName(param(method,0));const bool match=t&&std::strcmp(t,"Google.Protobuf.ByteString")==0;if(t)release(t);
            if(match){parseFrom=method;break;}
        }
    }
    if(!parseFrom)throw std::runtime_error("ParseFrom(ByteString) unavailable");
    BE_ResolvedMethodV1 parseMethod{};parseMethod.method_info=parseFrom;
    auto* message=api.call(parseMethod,parser,{data});
    if(!message||className(message)!=type)throw std::runtime_error("native parser rejected "+type);
    return message;
}
json inject(const json& command) {
    const auto type=command.at("type").get<std::string>();
    if(std::find(allowed.begin(),allowed.end(),type)==allowed.end())throw std::runtime_error("type not permitted: "+type);
    const auto base64=command.at("base64").get<std::string>();
    if(base64.size()>4*1024*1024)throw std::runtime_error("message too large");
    auto* message=parse(type,base64);eflink::unity::Api::TemporaryRoot messageRoot(runtime(),message);
    const int msgid=messageId(type);
    auto headClass=api.klass("CSHead","Beyond.Network.Proto.dll","Proto");
    auto* head=runtime()->object_new(runtime()->context,headClass.class_info);eflink::unity::Api::TemporaryRoot headRoot(runtime(),head);
    api.call(api.method("CSHead",".ctor","","System.Void","Beyond.Network.Proto.dll","Proto"),head);
    api.call(api.method("CSHead","set_Msgid","System.Int32","System.Void","Beyond.Network.Proto.dll","Proto"),head,{const_cast<int*>(&msgid)});
    // Without an observed bus, call the same native handler NetBus would select
    // for GameplayNetwork messages (the client's own _Handle_* method).
    static const std::array<std::pair<const char*,const char*>,7> handlers{{
        {"SC_OBJECT_ENTER_VIEW","_Handle_ObjectEnterView"},{"SC_OBJECT_LEAVE_VIEW","_Handle_ObjectLeaveView"},
        {"SC_SCENE_OBJECT_ENTER_VIEW_BEGIN","_Handle_ObjectEnterViewBegin"},{"SC_SCENE_OBJECT_ENTER_VIEW_END","_Handle_ObjectEnterViewEnd"},
        {"SC_SELF_SCENE_INFO","_Handle_SelfSceneInfo"},{"SC_ENTER_SCENE_NOTIFY","_Handle_EnterScene"},{"SC_CHANGE_SCENE_BEGIN_NOTIFY","_Handle_ChangeSceneBegin"}}};
    const auto via=command.value("via",std::string(bus?"bus":"handler"));
    if(via=="handler") {
        const char* handlerName=nullptr;for(const auto& [t,h]:handlers)if(type==t)handlerName=h;
        if(!handlerName)throw std::runtime_error("no GameplayNetwork handler for "+type);
        auto* instance=api.call(api.method("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Gameplay.Beyond.dll","Beyond.Gameplay"));
        auto* network=instance?api.call(api.method("GameInstance","get_gameplayNetwork","","Beyond.Gameplay.GameplayNetwork","Gameplay.Beyond.dll","Beyond.Gameplay"),instance):nullptr;
        if(!network)throw std::runtime_error("GameplayNetwork unavailable");
        const auto parameter=std::string("Proto.")+type;
        api.call(api.method("GameplayNetwork",handlerName,parameter.c_str(),"System.Void","Gameplay.Beyond.dll","Beyond.Gameplay"),network,{message});
        ++injected;
        return {{"type",type},{"msgid",msgid},{"dispatched",true},{"via","handler"}};
    }
    if(!bus)throw std::runtime_error("client receive bus not observed yet");
    alignas(8) unsigned char response[32]{};
    std::memcpy(response+headOffset,&head,sizeof head);
    std::memcpy(response+bodyOffset,&message,sizeof message);
    response[packedOffset]=0;
    const std::int64_t now=std::int64_t(GetTickCount64());std::memcpy(response+timestampOffset,&now,sizeof now);
    injecting=true;
    try {api.call(handleMethod,bus,{response});}
    catch(...) {injecting=false;++injectFailed;throw;}
    injecting=false;++injected;
    return {{"type",type},{"msgid",msgid},{"dispatched",true},{"via","bus"}};
}
void* objectContainer() {
    auto world=api.klass("GameWorld","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
    auto* field=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(world.class_info,"objectContainer");
    if(!field)throw std::runtime_error("objectContainer unavailable");
    void* value=nullptr;symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(field,&value);return value;
}
BE_ResolvedMethodV1 gm(const char* type,const char* name,const char* params="",const char* returns="System.Void",const char* space="Beyond.Gameplay.Core") {
    return api.method(type,name,params,returns,"Gameplay.Beyond.dll",space);
}
json scene() {
    const auto world=api.klass("GameWorld","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
    auto* field=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(world.class_info,"worldInfo");
    void* info=nullptr;if(field)symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(field,&info);
    if(!info)throw std::runtime_error("worldInfo unavailable");
    return {{"level_num",api.value<int>(gm("WorldInfo","get_curLevelIdNum","","System.Int32"),info)},
        {"map",text(api.call(gm("WorldInfo","get_curMapIdStr","","System.String"),info))},
        {"level",text(api.call(gm("WorldInfo","get_curLevelId","","System.String"),info))},
        {"started",api.value<bool>(gm("WorldInfo","get_isStarted","","System.Boolean"),info)}};
}
json describe(void* entity) {
    using V3=eflink::unity::V3;
    auto* data=api.call(gm("Entity","get_entityData","","Beyond.Gameplay.Core.BaseEntityData"),entity);
    const auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),entity);
    return {{"server_id",api.value<std::uint64_t>(gm("Entity","get_serverId","","System.UInt64"),entity)},
        {"instance_uid",api.value<std::uint32_t>(gm("Entity","get_instanceUid","","System.UInt32"),entity)},
        {"template",data?text(api.call(gm("BaseEntityData","get_templateId","","System.String"),data)):std::string{}},
        {"position",{p.x,p.y,p.z}},{"alive",api.value<bool>(gm("Entity","get_alive","","System.Boolean"),entity)},
        {"entity_battle_inst",api.value<std::uint64_t>(gm("Entity","get_serverBattleInstId","","System.UInt64"),entity)},
        {"data_battle_inst",data?api.value<std::uint64_t>(gm("BaseEntityData","get_serverBattleInstId","","System.UInt64"),data):0},
        {"generation",api.value<std::uint32_t>(gm("Entity","get_serverGeneration","","System.UInt32"),entity)}};
}
json enemies() {
    auto* container=objectContainer();if(!container)throw std::runtime_error("no world");
    using V3=eflink::unity::V3;json player=nullptr;
    if(auto* main=api.call(gm("PlayerController","GetMainCharacter","","Beyond.Gameplay.Core.Entity"))) {
        const auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),main);player={p.x,p.y,p.z};
    }
    auto* list=api.call(gm("ObjectContainer","get_allEnemies","","Beyond.UnorderedList<Beyond.Gameplay.Core.Entity>"),container);
    if(!list)return json::array();
    eflink::unity::Api::TemporaryRoot listRoot(runtime(),list);
    auto* klass=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(list);
    auto find=[&](const char* n,int c){auto* m=symbol<const void*(*)(const void*,const char*,int)>("il2cpp_class_get_method_from_name")(klass,n,c);
        if(!m)throw std::runtime_error(std::string("list method unavailable: ")+n);BE_ResolvedMethodV1 r{};r.method_info=m;return r;};
    const int n=api.value<int>(find("get_count",0),list);
    json out=json::array();
    for(int i=0;i<n&&i<256;++i)if(auto* entity=api.call(find("get_Item",1),list,{&i}))out.push_back(describe(entity));
    return {{"player",player},{"enemies",out}};
}
json entity(std::uint64_t serverId) {
    auto* container=objectContainer();if(!container)throw std::runtime_error("no world");
    auto* found=api.call(gm("ObjectContainer","GetEntityById","System.UInt64","Beyond.Gameplay.Core.Entity"),container,{&serverId});
    return found?json{{"exists",true},{"entity",describe(found)}}:json{{"exists",false}};
}
json attack();
json callers(const json& command=json::object());
// Read-only: who asks for battle sequence numbers (the op builders).
using NextSeq=int(__fastcall*)(void*,void*);NextSeq nextSeq=nullptr;BE_ResolvedMethodV1 seqMethod{};
std::map<std::uintptr_t,std::uint64_t> seqCallers;
using SetInt=void(__fastcall*)(void*,int,void*);SetInt nextSetSeq=nullptr;BE_ResolvedMethodV1 setSeqMethod{};
void __fastcall onSetSeq(void* self,int value,void* info) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    {std::lock_guard lock(mutex);if(seqCallers.size()<64||seqCallers.contains(caller))++seqCallers[caller];}
    if(nextSetSeq)nextSetSeq(self,value,info);
}
// Read-only probe of ServerReporter.CreateServerOp<__Canon>(opType, data, ref OwnerInfo).
using CreateOp=void(__fastcall*)(void*,int,void*,const std::uint64_t*,void*);CreateOp nextCreateOp=nullptr;
std::map<std::uint64_t,std::uint64_t> opOwners;std::uint64_t createOpCalls=0;
// Keep the actual stack for unattributed operations; the send stack is too late
// to identify which entity teardown produced them. This probe never suppresses
// operations or allocates a battle sequence number.
struct OpOrigin {
    int type=0;bool hasOwner=false;
    std::uint64_t serverId=0,battleInstance=0;
    std::array<std::uintptr_t,12> frames{};
    auto operator<=>(const OpOrigin&) const=default;
};
bool traceZeroOwners=false;std::uint64_t traceDeadline=0;std::map<OpOrigin,std::uint64_t> opOrigins;
std::uint64_t originOverflow=0;std::uintptr_t opHookRva=0;
void __fastcall onCreateOp(void* self,int type,void* data,const std::uint64_t* owner,void* info) {
    {
        std::lock_guard lock(mutex);++createOpCalls;
        if(owner&&(opOwners.size()<128||opOwners.contains(owner[0])))++opOwners[owner[0]];
        // CS_BATTLE_OP.owner_id is the battle instance, not the source entity
        // ID. Preserve both fields: a zero battle owner can still have a valid
        // server entity ID that lets the production filter identify its origin.
        if(traceZeroOwners&&GetTickCount64()<traceDeadline&&(!owner||owner[1]==0)) {
            OpOrigin origin;origin.type=type;origin.hasOwner=owner!=nullptr;
            if(owner){origin.serverId=owner[0];origin.battleInstance=owner[1];}
            void* frames[12]{};const auto depth=RtlCaptureStackBackTrace(1,12,frames,nullptr);
            for(USHORT i=0;i<depth;++i)origin.frames[i]=reinterpret_cast<std::uintptr_t>(frames[i]);
            if(opOrigins.size()<128||opOrigins.contains(origin))++opOrigins[origin];
            else ++originOverflow;
        }
    }
    if(nextCreateOp)nextCreateOp(self,type,data,owner,info);
}
int __fastcall onNextSeq(void* self,void* info) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    {std::lock_guard lock(mutex);if(seqCallers.size()<64||seqCallers.contains(caller))++seqCallers[caller];}
    return nextSeq?nextSeq(self,info):0;
}
json netState();
json moveNear(std::uint64_t serverId);
struct MetadataClass {const char* assembly;const char* space;};
MetadataClass metadataClass(const std::string& klass) {
    if(klass=="HGNetSession"||klass=="NetClientManager")return {"Network.Beyond.dll","Beyond.Network"};
    if(klass=="GameplayNetwork"||klass=="GameInstance"||klass=="CharBagSystem")return {"Gameplay.Beyond.dll","Beyond.Gameplay"};
    if(klass=="GameLevelLoader"||klass=="WorldInfo"||klass=="EntityManager"||klass=="CharacterEntityData"||klass=="SquadManager"||
       klass=="BattleManager/ServerReporter"||klass=="AbilitySystem"||klass=="Entity")return {"Gameplay.Beyond.dll","Beyond.Gameplay.Core"};
    throw std::runtime_error("metadata diagnostic class not permitted");
}
json classMetadata(const json& command) {
    const auto klass=command.at("class").get<std::string>();const auto scope=metadataClass(klass);
    const auto resolved=api.klass(klass.c_str(),scope.assembly,scope.space);
    auto typeName=symbol<char*(*)(const void*)>("il2cpp_type_get_name");
    auto release=symbol<void(*)(void*)>("il2cpp_free");
    auto typeText=[&](const void* type) {char* value=typeName(type);std::string text_=value?value:"";if(value)release(value);return text_;};
    auto methods=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_methods");
    auto name=symbol<const char*(*)(const void*)>("il2cpp_method_get_name");
    auto count=symbol<unsigned(*)(const void*)>("il2cpp_method_get_param_count");
    auto param=symbol<const void*(*)(const void*,unsigned)>("il2cpp_method_get_param");
    auto result=symbol<const void*(*)(const void*)>("il2cpp_method_get_return_type");
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const auto limit=base+pe->OptionalHeader.SizeOfImage;
    json out={{"class",klass},{"assembly",scope.assembly},{"namespace",scope.space},{"methods",json::array()},{"fields",json::array()},{"gameplay_modified",false}};
    void* it=nullptr;std::size_t emitted=0;
    while(auto* method=methods(resolved.class_info,&it)) {
        if(++emitted>512)throw std::runtime_error("class metadata method limit exceeded");
        json parameters=json::array();for(unsigned i=0,n=count(method);i<n;++i)parameters.push_back(typeText(param(method,i)));
        const auto address=*reinterpret_cast<const std::uintptr_t*>(method);
        out["methods"].push_back({{"name",name(method)},{"parameters",parameters},{"return",typeText(result(method))},{"rva",address>=base&&address<limit?address-base:0}});
    }
    auto fields=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_fields");
    auto fieldName=symbol<const char*(*)(const void*)>("il2cpp_field_get_name");
    auto fieldType=symbol<const void*(*)(const void*)>("il2cpp_field_get_type");
    auto offset=symbol<std::size_t(*)(const void*)>("il2cpp_field_get_offset");
    it=nullptr;emitted=0;
    while(auto* field=fields(resolved.class_info,&it)) {
        if(++emitted>256)throw std::runtime_error("class metadata field limit exceeded");
        out["fields"].push_back({{"name",fieldName(field)},{"type",typeText(fieldType(field))},{"offset",offset(field)}});
    }
    return out;
}
json methodCode(const json& command) {
    const auto klass=command.at("class").get<std::string>();
    const auto name=command.at("method").get<std::string>();
    const auto parameters=command.value("parameters",std::string{});
    const auto result=command.value("return",std::string("System.Void"));
    const auto scope=metadataClass(klass);
    const auto method=api.method(klass.c_str(),name.c_str(),parameters.c_str(),result.c_str(),scope.assembly,scope.space);
    if(!method.method_pointer)throw std::runtime_error("method has no native code");
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const auto start=reinterpret_cast<std::uintptr_t>(method.method_pointer);
    const auto limit=base+pe->OptionalHeader.SizeOfImage;
    MEMORY_BASIC_INFORMATION page{};
    if(start<base||start>=limit||!VirtualQuery(method.method_pointer,&page,sizeof page)||page.State!=MEM_COMMIT||
        !(page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
        throw std::runtime_error("method is outside executable GameAssembly code");
    const auto pageLimit=reinterpret_cast<std::uintptr_t>(page.BaseAddress)+page.RegionSize;
    const auto count=(std::min)(std::size_t(2048),(std::min)(std::size_t(limit-start),std::size_t(pageLimit-start)));
    constexpr char hex[]="0123456789abcdef";std::string bytes;bytes.reserve(count*2);
    const auto* code=static_cast<const unsigned char*>(method.method_pointer);
    for(std::size_t i=0;i<count;++i){bytes+=hex[code[i]>>4];bytes+=hex[code[i]&15];}
    return {{"rva",start-base},{"code_hex",bytes},{"gameplay_modified",false}};
}
// Data-layer view: an entity may exist as committed data before the AOI spawns it.
json node(std::uint64_t serverId) {
    auto klass=api.klass("EntityManager","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
    auto* field=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,"s_manager");
    void* manager=nullptr;if(field)symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(field,&manager);
    if(!manager)throw std::runtime_error("EntityManager unavailable");
    void* data=nullptr;
    const bool found=api.value<bool>(gm("EntityManager","TryGetEntityData","System.UInt64|Beyond.Gameplay.Core.BaseEntityData&","System.Boolean"),manager,{&serverId,&data});
    json out={{"data",found&&data}};
    if(found&&data) {
        out["template"]=text(api.call(gm("BaseEntityData","get_templateId","","System.String"),data));
        out["committed"]=api.value<bool>(gm("BaseEntityData","get_isCommitted","","System.Boolean"),data);
        out["has_entity"]=api.call(gm("BaseEntityData","GetEntity","","Beyond.Gameplay.Core.Entity"),data)!=nullptr;
    }
    void* info=nullptr;
    if(api.value<bool>(gm("EntityManager","TryGetEntityLoadInfo","System.UInt64|System.String&","System.Boolean"),manager,{&serverId,&info}))
        out["load_info"]=text(info).substr(0,600);
    return out;
}
void runCommands() {
    std::deque<json> pending;{std::lock_guard lock(mutex);pending.swap(commands);}
    for(auto& command:pending) {
        json record={{"action",command.value("action",std::string{})},{"at",GetTickCount64()},
            {"command_id",command.value("command_id",std::string{})}};
        try {
            const auto action=command.at("action").get<std::string>();
            if(action=="inject")record["result"]=inject(command);
            else if(action=="enemies")record["result"]=enemies();
            else if(action=="entity")record["result"]=entity(command.at("id").get<std::uint64_t>());
            else if(action=="node")record["result"]=node(command.at("id").get<std::uint64_t>());
            else if(action=="attack")record["result"]=attack();
            else if(action=="net")record["result"]=netState();
            else if(action=="callers")record["result"]={{"callers",callers(command)}};
            else if(action=="code")record["result"]=methodCode(command);
            else if(action=="metadata")record["result"]=classMetadata(command);
            else if(action=="scene")record["result"]=scene();
            else if(action=="goto")record["result"]=moveNear(command.at("id").get<std::uint64_t>());
            else throw std::runtime_error("unknown command");
        }catch(const std::exception& e){record["error"]=e.what();}
        std::lock_guard lock(mutex);
        if(results.size()>=16)results.erase(results.begin());
        results.push_back(std::move(record));
    }
}
void __fastcall onHandle(void* self,const void* response,void* info) {
    if(!injecting&&self&&(!info||info==handleMethod.method_info)) {
        std::lock_guard lock(mutex);
        if(bus!=self) {
            if(busRoot)runtime()->gchandle_free(runtime()->context,busRoot);
            bus=self;busRoot=runtime()->gchandle_new(runtime()->context,self,0);
        }
        ++realMessages;
        // Copy whitelisted world messages (body bytes only) for local analysis.
        if(codecReady&&response&&captured.size()<64)try {
            void* body=nullptr;std::memcpy(&body,static_cast<const unsigned char*>(response)+bodyOffset,sizeof body);
            const auto type=className(body);
            if(std::find(capturedTypes.begin(),capturedTypes.end(),type)!=capturedTypes.end())
                captured.push_back({{"type",type},{"at",GetTickCount64()},{"protobuf_base64",codec.serialize(body)}});
        }catch(...) {++captureFailed;}
    }
    if(nextHandle)nextHandle(self,response,info);
}
// Observe (never alter) what this client reports to its official server.
std::uint64_t __fastcall onSend(void* self,void* message,void* info) {
    if(message&&(!info||info==sendMethod.method_info))try {
        const auto name=className(message);
        if(name=="CS_SCENE_LOAD_FINISH") {
            std::lock_guard lock(mutex);++sentSceneLoadFinishes;
            if(codecReady&&sent.size()<64)sent.push_back({{"type",name},{"at",GetTickCount64()},{"protobuf_base64",codec.serialize(message)}});
        } else if(name=="CS_BATTLE_OP") {
            void* frames[10]{};const auto depth=RtlCaptureStackBackTrace(1,10,frames,nullptr);
            std::lock_guard lock(mutex);++sentBattleOps;
            for(USHORT i=0;i<depth;++i) {
                const auto address=reinterpret_cast<std::uintptr_t>(frames[i])+std::uintptr_t(i)*0; // frame order kept below
                if(seqCallers.size()<64||seqCallers.contains(address))++seqCallers[address];
            }
            if(codecReady&&sent.size()<64)sent.push_back({{"type","CS_BATTLE_OP"},{"at",GetTickCount64()},{"protobuf_base64",codec.serialize(message)}});
        }
    }catch(...) {}
    return nextSend?nextSend(self,message,info):0;
}
json attack() {
    auto* main=api.call(gm("PlayerController","GetMainCharacter","","Beyond.Gameplay.Core.Entity"));
    auto* ability=main?api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),main):nullptr;
    if(!ability)throw std::runtime_error("main character unavailable");
    return {{"accepted",api.value<bool>(gm("AbilitySystem","TryCastNormalAttack","","System.Boolean"),ability)}};
}
json moveNear(std::uint64_t serverId) {
    using V3=eflink::unity::V3;
    auto* container=objectContainer();
    auto* target=api.call(gm("ObjectContainer","GetEntityById","System.UInt64","Beyond.Gameplay.Core.Entity"),container,{&serverId});
    auto* main=api.call(gm("PlayerController","GetMainCharacter","","Beyond.Gameplay.Core.Entity"));
    if(!target||!main)throw std::runtime_error("target or main character unavailable");
    auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),target);p.x+=2.5f;
    auto* movement=api.call(gm("Entity","get_movementComponent","","Beyond.Gameplay.Core.MovementComponent"),main);
    bool warning=false;api.call(gm("MovementComponent","TeleportTo","UnityEngine.Vector3|System.Boolean"),movement,{&p,&warning});
    return {{"position",{p.x,p.y,p.z}}};
}
json netState() {
    auto* instance=api.call(api.method("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Gameplay.Beyond.dll","Beyond.Gameplay"));
    auto klass=api.klass("GameInstance","Gameplay.Beyond.dll","Beyond.Gameplay");
    auto* field=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,"netClientManager");
    void* manager=nullptr;if(field)symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(field,&manager);
    json out={{"game_instance",instance!=nullptr},{"net_manager",manager!=nullptr}};
    if(manager) {
        out["session_running"]=api.value<bool>(api.method("NetClientManager","get_isNetSessionRunning","","System.Boolean","Network.Beyond.dll","Beyond.Network"),manager);
        auto* session=*reinterpret_cast<void**>(static_cast<unsigned char*>(manager)+120);
        if(session) {
            out["ping"]=api.value<int>(api.method("HGNetSession","GetPing","","System.Int32","Network.Beyond.dll","Beyond.Network"),session);
            out["ping_out"]=api.value<bool>(api.method("HGNetSession","IsPingOut","","System.Boolean","Network.Beyond.dll","Beyond.Network"),session);
        }
    }
    out["time_scale"]=api.value<float>(api.method("Time","get_timeScale","","System.Single"));
    out["focused"]=api.value<bool>(api.method("Application","get_isFocused","","System.Boolean"));
    return out;
}
// Map code addresses to the IL2CPP method containing them (metadata scan only).
json callers(const json& command) {
    auto domainGet=symbol<void*(*)()>("il2cpp_domain_get");
    auto assemblies=symbol<void**(*)(void*,std::size_t*)>("il2cpp_domain_get_assemblies");
    auto imageOf=symbol<const void*(*)(const void*)>("il2cpp_assembly_get_image");
    auto imageName=symbol<const char*(*)(const void*)>("il2cpp_image_get_name");
    auto classCount=symbol<std::size_t(*)(const void*)>("il2cpp_image_get_class_count");
    auto classAt=symbol<const void*(*)(const void*,std::size_t)>("il2cpp_image_get_class");
    auto methods=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_methods");
    auto methodName=symbol<const char*(*)(const void*)>("il2cpp_method_get_name");
    auto klassName=symbol<const char*(*)(const void*)>("il2cpp_class_get_name");
    auto klassSpace=symbol<const char*(*)(const void*)>("il2cpp_class_get_namespace");
    auto declaring=symbol<const void*(*)(const void*)>("il2cpp_class_get_declaring_type");
    std::map<std::uintptr_t,std::uint64_t> wanted;{std::lock_guard lock(mutex);wanted=seqCallers;}
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const auto limit=base+pe->OptionalHeader.SizeOfImage;
    if(command.contains("rvas")) {
        const auto& rvas=command.at("rvas");
        if(!rvas.is_array()||rvas.size()>128)throw std::runtime_error("method lookup requires at most 128 RVAs");
        for(const auto& value:rvas) {
            const auto rva=value.get<std::uint64_t>();
            if(!rva||rva>=limit-base)throw std::runtime_error("method lookup RVA is outside GameAssembly");
            wanted[base+std::uintptr_t(rva)]=0;
        }
    }
    {std::lock_guard lock(mutex);for(const auto& [origin,hits]:opOrigins)
        for(auto address:origin.frames)if(address>=base&&address<limit)wanted[address]+=hits;}
    // Only native methods in GameAssembly can be named by IL2CPP metadata.
    for(auto it=wanted.begin();it!=wanted.end();)if(it->first<base||it->first>=limit)it=wanted.erase(it);else ++it;
    std::map<std::uintptr_t,std::pair<std::uintptr_t,std::string>> best;
    std::size_t count=0;auto** list=assemblies(domainGet(),&count);
    for(std::size_t a=0;a<count;++a) {
        auto* image=imageOf(list[a]);const std::string img=imageName(image);
        if(img.rfind("Unity",0)==0||img.rfind("System",0)==0||img=="mscorlib.dll")continue;
        const auto n=classCount(image);
        for(std::size_t c=0;c<n;++c) {
            auto* klass=classAt(image,c);void* it=nullptr;
            while(auto* method=methods(klass,&it)) {
                const auto start=*reinterpret_cast<const std::uintptr_t*>(method);if(!start)continue;
                for(const auto& [address,hits]:wanted)if(start<=address&&(!best.contains(address)||start>best[address].first)) {
                    std::string name=std::string(klassSpace(klass))+".";
                    if(auto* outer=declaring(klass))name+=std::string(klassName(outer))+"/";
                    name+=std::string(klassName(klass))+"::"+methodName(method);
                    best[address]={start,name};
                }
            }
        }
    }
    json out=json::array();
    for(const auto& [address,hits]:wanted)out.push_back({{"rva",address-base},{"calls",hits},
        {"method",best.contains(address)?best[address].second:std::string{}},
        {"method_is_nearest_candidate",true},{"exact_method_start",best.contains(address)&&best[address].first==address},
        {"offset",best.contains(address)?address-best[address].first:0}});
    return out;
}
void __fastcall onTick(void* self,float delta,void* info) {
    if(nextTick)nextTick(self,delta,info);
    if(!self||(info&&info!=tickMethod.method_info)||className(self)!="CameraManager")return;
    try {api.bind(runtime());runCommands();}catch(...) {}
}
void install() {
    if(installed)return;
    RuntimeThreadScope attached(true);
    auto* rt=runtime();if(!attached.ready()||!rt||!host.hooks||!host.hooks->create)throw std::runtime_error("runtime/hook chain unavailable");
    api.bind(rt);
    BE_ResolvedClassV1 responseClass{};
    if(rt->resolve_class(rt->context,"Network.Beyond.dll","Beyond.Network","NetResponse",&responseClass)!=BE_Result_Ok)throw std::runtime_error("NetResponse unresolved");
    unsigned alignment=0;
    if(!symbol<bool(*)(const void*)>("il2cpp_class_is_valuetype")(responseClass.class_info)||
       symbol<int(*)(const void*,unsigned*)>("il2cpp_class_value_size")(responseClass.class_info,&alignment)!=32||alignment!=8)
        throw std::runtime_error("NetResponse ABI differs");
    // Boxed IL2CPP offsets include the 16-byte object header; the struct does not.
    auto offset=[&](const char* field,const char* type) {
        BE_FieldDescriptorV1 d{"Network.Beyond.dll","Beyond.Network","NetResponse",field,type};BE_ResolvedFieldV1 r{};
        if(rt->resolve_field(rt->context,&d,&r)!=BE_Result_Ok||r.offset<16)throw std::runtime_error(std::string("NetResponse field unresolved: ")+field);
        return std::size_t(r.offset)-16;
    };
    headOffset=offset("headMsg","Google.Protobuf.IMessage");bodyOffset=offset("msgBody","Google.Protobuf.IMessage");
    packedOffset=offset("isPacked","System.Boolean");timestampOffset=offset("recvTimestamp","System.Int64");
    if(headOffset+8>32||bodyOffset+8>32||packedOffset>=32||timestampOffset+8>32)throw std::runtime_error("NetResponse layout out of range");
    BE_MethodDescriptorV1 handle{"Network.Beyond.dll","Beyond.Network","NetBus","HandleMessage","Beyond.Network.NetResponse","System.Void",1};
    BE_MethodDescriptorV1 tick{"Gameplay.Beyond.dll","Beyond.Gameplay.View","CameraManager","TailLateTick","System.Single","System.Void",1};
    if(rt->resolve_method(rt->context,&handle,&handleMethod)!=BE_Result_Ok||!handleMethod.method_pointer||
       rt->resolve_method(rt->context,&tick,&tickMethod)!=BE_Result_Ok||!tickMethod.method_pointer)throw std::runtime_error("methods unresolved");
    std::uint64_t h=0;
    if(host.hooks->create(host.hooks->context,id,handleMethod.method_pointer,reinterpret_cast<void*>(onHandle),reinterpret_cast<void**>(&nextHandle),&h)!=BE_Result_Ok)
        throw std::runtime_error("receive hook failed");
    if(host.hooks->create(host.hooks->context,id,tickMethod.method_pointer,reinterpret_cast<void*>(onTick),reinterpret_cast<void**>(&nextTick),&h)!=BE_Result_Ok) {
        host.hooks->disable_module(host.hooks->context,id);throw std::runtime_error("tick hook failed");
    }
    BE_MethodDescriptorV1 send{"Network.Beyond.dll","Beyond.Network","HGNetSession","SendMessage","Google.Protobuf.IMessage","System.UInt64",1};
    if(rt->resolve_method(rt->context,&send,&sendMethod)==BE_Result_Ok&&sendMethod.method_pointer)
        host.hooks->create(host.hooks->context,id,sendMethod.method_pointer,reinterpret_cast<void*>(onSend),reinterpret_cast<void**>(&nextSend),&h);
    BE_MethodDescriptorV1 seq{"Gameplay.Beyond.dll","Beyond.Gameplay","GameplayNetwork","GetNextBattleSeqId",nullptr,"System.Int32",0};
    if(rt->resolve_method(rt->context,&seq,&seqMethod)==BE_Result_Ok&&seqMethod.method_pointer)
        host.hooks->create(host.hooks->context,id,seqMethod.method_pointer,reinterpret_cast<void*>(onNextSeq),reinterpret_cast<void**>(&nextSeq),&h);
    BE_MethodDescriptorV1 setSeq{"Beyond.Network.Proto.dll","Proto","BATTLE_CLIENT_OP_DATA","set_SeqId","System.Int32","System.Void",1};
    if(rt->resolve_method(rt->context,&setSeq,&setSeqMethod)==BE_Result_Ok&&setSeqMethod.method_pointer)
        host.hooks->create(host.hooks->context,id,setSeqMethod.method_pointer,reinterpret_cast<void*>(onSetSeq),reinterpret_cast<void**>(&nextSetSeq),&h);
    try {codec.bind(rt);codecReady=true;}catch(...) {codecReady=false;}
    installed=true;
}
BE_Result BE_CALL initialize(const BE_ThirdPartyHostV1* input,const char*) {
    std::lock_guard lock(mutex);
    if(!input||input->version!=1||!input->module_id||std::string(input->module_id)!=id||!input->reply||
       input->struct_size<offsetof(BE_ThirdPartyHostV1,emit)+sizeof(input->emit))return BE_Result_ContractMismatch;
    host={};std::memcpy(&host,input,(std::min)(std::size_t(input->struct_size),sizeof(host)));
    return BE_Result_Ok; // Hooks are installed only by an explicit "start".
}
BE_Result BE_CALL configure(const char*) {return BE_Result_Ok;}
BE_Result BE_CALL message(const char* request,const char* body) {
    json result;BE_Result code=BE_Result_Ok;
    try {
        auto input=json::parse(body);const auto action=input.value("action",std::string("status"));
        std::lock_guard lock(mutex);
        if(action=="start")install();
        else if(action=="inject"||action=="enemies"||action=="entity"||action=="node"||action=="attack"||action=="goto"||action=="net"||action=="callers"||action=="code"||action=="metadata"||action=="scene") {
            if(!installed)throw std::runtime_error("start first");
            if(commands.size()>=16)throw std::runtime_error("queue full");
            input["action"]=action;commands.push_back(std::move(input));
        } else if(action=="hookop") {
            const auto rva=input.at("rva").get<std::uintptr_t>();
            if(!installed)throw std::runtime_error("start first");
            if(opHookRva)throw std::runtime_error("operation hook already installed; use a new module generation");
            const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"));
            const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
            if(!rva||rva>=pe->OptionalHeader.SizeOfImage)throw std::runtime_error("operation RVA outside GameAssembly");
            auto* target=reinterpret_cast<void*>(base+rva);
            MEMORY_BASIC_INFORMATION page{};
            if(!VirtualQuery(target,&page,sizeof page)||page.State!=MEM_COMMIT||
                !(page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
                throw std::runtime_error("operation target is not executable");
            std::uint64_t h=0;
            const bool hooked=host.hooks->create(host.hooks->context,id,target,reinterpret_cast<void*>(onCreateOp),reinterpret_cast<void**>(&nextCreateOp),&h)==BE_Result_Ok;
            if(hooked)opHookRva=rva;
            result={{"hooked",hooked}};
            auto text=result.dump();return host.reply(host.context,request,code,text.c_str());
        } else if(action=="traceops") {
            const auto seconds=input.value("seconds",300u);
            if(!seconds||seconds>300)throw std::runtime_error("trace duration must be 1..300 seconds");
            traceZeroOwners=input.value("enabled",false);
            traceDeadline=traceZeroOwners?GetTickCount64()+std::uint64_t(seconds)*1000:0;
            if(input.value("reset",false)) {opOrigins.clear();opOwners.clear();seqCallers.clear();createOpCalls=0;originOverflow=0;sentBattleOps=0;sent=json::array();}
        } else if(action=="optraces") {
            json origins=json::array();
            const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"));
            const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
            const auto limit=base+pe->OptionalHeader.SizeOfImage;
            for(const auto& [origin,hits]:opOrigins) {
                json frames=json::array();
                for(auto address:origin.frames)if(address>=base&&address<limit)frames.push_back(address-base);
                origins.push_back({{"type",origin.type},{"has_owner",origin.hasOwner},
                    {"source_server_id",origin.serverId},{"battle_instance",origin.battleInstance},
                    {"calls",hits},{"frames_rva",frames}});
            }
            result={{"enabled",traceZeroOwners&&GetTickCount64()<traceDeadline},{"overflow",originOverflow},{"origins",origins}};
            auto text=result.dump();return host.reply(host.context,request,code,text.c_str());
        } else if(action=="ops") {
            json owners=json::array();for(const auto& [o,n]:opOwners)owners.push_back({{"server_id",o},{"ops",n}});
            result={{"calls",createOpCalls},{"owners",owners}};
            auto text=result.dump();return host.reply(host.context,request,code,text.c_str());
        } else if(action=="drain") {
            result={{"messages",captured},{"sent",sent}};captured=json::array();sent=json::array();
            auto text=result.dump(-1,' ',false,json::error_handler_t::replace);return host.reply(host.context,request,code,text.c_str());
        } else if(action!="status")throw std::runtime_error("unknown action");
        result={{"installed",installed},{"bus_observed",bus!=nullptr},{"real_messages",realMessages},{"injected",injected},
            {"inject_failed",injectFailed},{"queued",commands.size()},{"captured",captured.size()},{"capture_failed",captureFailed},
            {"sent_battle_ops",sentBattleOps},{"sent_scene_load_finishes",sentSceneLoadFinishes},
            {"trace_ops_active",traceZeroOwners&&GetTickCount64()<traceDeadline},{"results",results},{"sends_to_server",false}};
    }catch(const std::exception& e){result={{"error",e.what()},{"sends_to_server",false}};code=BE_Result_Failed;}
    auto text=result.dump(-1,' ',false,json::error_handler_t::replace);return host.reply(host.context,request,code,text.c_str());
}
void BE_CALL shutdown() {
    std::lock_guard lock(mutex);
    if(host.hooks&&host.hooks->disable_module)host.hooks->disable_module(host.hooks->context,id);
    if(busRoot&&runtime())runtime()->gchandle_free(runtime()->context,busRoot);
    bus=nullptr;busRoot=0;installed=false;commands.clear();
}
const BE_ThirdPartyModuleV1 moduleApi{sizeof(BE_ThirdPartyModuleV1),1,id,initialize,configure,message,shutdown};
}
extern "C" __declspec(dllexport) const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1(){return &moduleApi;}
