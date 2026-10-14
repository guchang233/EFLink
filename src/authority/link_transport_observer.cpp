// Development evidence only: observes message TYPE NAMES and forwards every call.
// Default build records types only. The explicit development world-wire build
// serializes a bounded whitelist of gameplay messages; neither mutates worlds.
#include "BetterEndfield/ThirdPartyModule.h"
#include "../runtime_scope.h"
#include "native_proto_codec.h"
#include "native_build_policy.h"
#include "nlohmann/json.hpp"
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {
using nlohmann::json;
#ifndef EFLINK_OBSERVER_ID
#define EFLINK_OBSERVER_ID "eflink.transport1"
#endif
constexpr char id[]=EFLINK_OBSERVER_ID;
BE_ThirdPartyHostV1 host{};
std::mutex lifecycle,countsMutex;
std::atomic<bool> observing=false;
std::map<std::string,std::uint64_t> inbound,outbound;
std::atomic<std::uint64_t> inboundCalls=0,outboundCalls=0,unclassified=0;
BE_ResolvedMethodV1 receiveMethod{},sendMethod{};
using Receive=void(__fastcall*)(void*,const void*,void*);
using Send=std::uint64_t(__fastcall*)(void*,void*,void*);
Receive nextReceive=nullptr;
Send nextSend=nullptr;
using ClassOf=const void*(*)(void*);
using ClassText=const char*(*)(const void*);
ClassOf classOf=nullptr;
ClassText className=nullptr,classNamespace=nullptr;
std::size_t bodyOffset=0;
#ifdef EFLINK_CAPTURE_WORLD_WIRE
eflink::authority::NativeProtoCodec codec;
json captured=json::array();
std::size_t capturedBytes=0;
std::atomic<std::uint64_t> captureFailed=0,captureDropped=0;
std::map<std::string,std::size_t> captureCounts;
std::atomic<std::uint64_t> captureDeadline=0;
bool selected(const std::string& type,bool received) {
    if(!received)return type=="CS_BATTLE_OP"||type=="CS_SCENE_LOAD_FINISH";
    return type=="SC_SELF_SCENE_INFO"||type=="SC_OBJECT_ENTER_VIEW"||type=="SC_CHANGE_SCENE_BEGIN_NOTIFY"||
        type=="SC_ENTER_SCENE_NOTIFY"||type=="SC_ENTITY_PROPERTY_CHANGE"||type=="SC_BATTLE_GENERATION_CHANGE";
}
#endif

template<class T> T symbol(HMODULE module,const char* name) {
    auto p=reinterpret_cast<T>(GetProcAddress(module,name));
    if(!p)throw std::runtime_error(std::string("missing metadata export: ")+name);
    return p;
}

// Isolate ABI/address faults without changing exceptions from the original call.
const void* safeClass(void* object) noexcept {
    __try {return object&&classOf?classOf(object):nullptr;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
}
void* safeBody(const void* response) noexcept {
    __try {return response?*reinterpret_cast<void* const*>(static_cast<const char*>(response)+bodyOffset):nullptr;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
}
void record(void* message,bool received) noexcept {
    if(!observing.load(std::memory_order_relaxed))return;
#ifdef EFLINK_CAPTURE_WORLD_WIRE
    if(GetTickCount64()>=captureDeadline.load()) {observing=false;return;}
#endif
    try {
        const auto* klass=safeClass(message);
        if(!klass) {++unclassified;return;}
        const auto* ns=classNamespace(klass);const auto* name=className(klass);
        if(!ns||!name||std::strcmp(ns,"Proto")!=0) {++unclassified;return;}
        // No field values, protocol payloads, login tokens or account identifiers.
        std::string type=name;
        if(type.size()>200) {++unclassified;return;}
        std::lock_guard lock(countsMutex);
        auto& counts=received?inbound:outbound;
        if(counts.size()<512||counts.contains(type))++counts[type];else ++unclassified;
#ifdef EFLINK_CAPTURE_WORLD_WIRE
        if(selected(type,received)) {
            if(captureCounts[type]>=64) {
                for(auto item=captured.begin();item!=captured.end();++item)if(item->at("type")==type) {
                    capturedBytes-=item->at("protobuf_base64").get_ref<const std::string&>().size();
                    captured.erase(item);--captureCounts[type];++captureDropped;break;
                }
            }
            if(captured.size()>=512||capturedBytes>=16*1024*1024) {++captureDropped;return;}
            try {
                auto bytes=codec.serialize(message);
                if(bytes.size()>16*1024*1024-capturedBytes) {++captureDropped;return;}
                capturedBytes+=bytes.size();
                ++captureCounts[type];
                captured.push_back({{"type",type},{"received",received},{"protobuf_base64",std::move(bytes)}});
            } catch(...) {++captureFailed;}
        }
#endif
    }catch(...) {++unclassified;}
}
void __fastcall receive(void* self,const void* response,void* info) {
    if(observing.load()&&(!info||info==receiveMethod.method_info)) {
        ++inboundCalls;record(safeBody(response),true);
    }
    if(nextReceive)nextReceive(self,response,info);
}
std::uint64_t __fastcall send(void* self,void* message,void* info) {
    if(observing.load()&&(!info||info==sendMethod.method_info)) {
        ++outboundCalls;record(message,false);
    }
    return nextSend?nextSend(self,message,info):0;
}
const BE_HostApiV1* runtime() {return host.get_runtime?host.get_runtime(host.context):host.runtime;}
void disable() {
    observing=false;
    if(host.hooks&&host.hooks->disable_module)host.hooks->disable_module(host.hooks->context,id);
}
void start() {
    if(observing)return;
    RuntimeThreadScope attached(true);
    auto* api=runtime();auto module=GetModuleHandleW(L"GameAssembly.dll");
    if(!attached.ready()||!api||!module||!host.hooks||!host.hooks->create||!host.hooks->disable_module)
        throw std::runtime_error("runtime/shared hook chain unavailable");
    if(!eflink::worldauthority::readOnlyBuildSupported(eflink::worldauthority::nativeGameVersion(api)))
        throw std::runtime_error("unverified game build; native transport observation disabled");
    classOf=symbol<ClassOf>(module,"il2cpp_object_get_class");
    className=symbol<ClassText>(module,"il2cpp_class_get_name");
    classNamespace=symbol<ClassText>(module,"il2cpp_class_get_namespace");
#ifdef EFLINK_CAPTURE_WORLD_WIRE
    codec.bind(api);
    captureDeadline=GetTickCount64()+300000;
#endif
    BE_ResolvedClassV1 response{};
    if(api->resolve_class(api->context,"Network.Beyond.dll","Beyond.Network","NetResponse",&response)!=BE_Result_Ok)
        throw std::runtime_error("NetResponse unresolved");
    unsigned alignment=0;
    auto isValue=symbol<bool(*)(const void*)>(module,"il2cpp_class_is_valuetype");
    auto size=symbol<int(*)(const void*,unsigned*)>(module,"il2cpp_class_value_size");
    if(!isValue(response.class_info)||size(response.class_info,&alignment)!=32||alignment!=8)
        throw std::runtime_error("NetResponse ABI differs; observer not installed");
    BE_FieldDescriptorV1 body{"Network.Beyond.dll","Beyond.Network","NetResponse","msgBody","Google.Protobuf.IMessage"};
    BE_ResolvedFieldV1 field{};
    if(api->resolve_field(api->context,&body,&field)!=BE_Result_Ok||field.offset!=24)
        throw std::runtime_error("NetResponse body layout differs; observer not installed");
    // IL2CPP value-type field offsets include the 16-byte boxed object header.
    // MSVC x64 passes this 32-byte value parameter through a pointer to unboxed data.
    bodyOffset=static_cast<std::size_t>(field.offset)-2*sizeof(void*);
    BE_MethodDescriptorV1 rx{"Network.Beyond.dll","Beyond.Network","NetBus","HandleMessage","Beyond.Network.NetResponse","System.Void",1};
    BE_MethodDescriptorV1 tx{"Network.Beyond.dll","Beyond.Network","HGNetSession","SendMessage","Google.Protobuf.IMessage","System.UInt64",1};
    if(api->resolve_method(api->context,&rx,&receiveMethod)!=BE_Result_Ok||
       api->resolve_method(api->context,&tx,&sendMethod)!=BE_Result_Ok||
       !receiveMethod.method_pointer||!sendMethod.method_pointer||receiveMethod.method_pointer==sendMethod.method_pointer)
        throw std::runtime_error("transport methods unresolved");
    std::uint64_t handle=0;
    if(host.hooks->create(host.hooks->context,id,receiveMethod.method_pointer,reinterpret_cast<void*>(receive),reinterpret_cast<void**>(&nextReceive),&handle)!=BE_Result_Ok)
        throw std::runtime_error("receive observation registration failed");
    if(host.hooks->create(host.hooks->context,id,sendMethod.method_pointer,reinterpret_cast<void*>(send),reinterpret_cast<void**>(&nextSend),&handle)!=BE_Result_Ok) {
        disable();throw std::runtime_error("send observation registration failed");
    }
    observing=true;
}
BE_Result BE_CALL initialize(const BE_ThirdPartyHostV1* input,const char*) {
    std::lock_guard lock(lifecycle);
    if(!input||input->version!=1||!input->module_id||std::string(input->module_id)!=id||!input->reply||
        input->struct_size<offsetof(BE_ThirdPartyHostV1,emit)+sizeof(input->emit))return BE_Result_ContractMismatch;
    host={};std::memcpy(&host,input,(std::min)(std::size_t(input->struct_size),sizeof(host)));
    // Registration alone does not install hooks. The explicit start RPC does.
    return BE_Result_Ok;
}
BE_Result BE_CALL configure(const char*) {return BE_Result_Ok;}
BE_Result BE_CALL message(const char* request,const char* text) {
    std::lock_guard lock(lifecycle);
    json result;BE_Result code=BE_Result_Ok;
    try {
        auto action=json::parse(text).value("action",std::string("status"));
        if(action=="start")start();
        else if(action=="stop")disable();
        else if(action!="status"
#ifdef EFLINK_CAPTURE_WORLD_WIRE
            &&action!="drain"
#endif
        )throw std::runtime_error("unknown observer action");
        std::lock_guard countLock(countsMutex);
        result={{"observer_only",true},{"world_state_modified",false},{"payloads_recorded",false},
            {"observing",observing.load()},{"receive_calls",inboundCalls.load()},{"send_calls",outboundCalls.load()},
            {"unclassified",unclassified.load()},{"incoming_types",inbound},{"outgoing_types",outbound},
            {"authority_established",false}};
#ifdef EFLINK_CAPTURE_WORLD_WIRE
        result["payloads_recorded"]=true;
        result["bounded_world_messages_only"]=true;
        result["captured_messages"]=captured.size();result["captured_base64_bytes"]=capturedBytes;
        result["capture_failed"]=captureFailed.load();result["capture_dropped"]=captureDropped.load();
        if(action=="drain") {
            if(observing)throw std::runtime_error("stop observation before draining");
            result["messages"]=std::move(captured);captured=json::array();capturedBytes=0;captureCounts.clear();
        }
#endif
    }catch(const std::exception& e) {result={{"error",e.what()},{"authority_established",false}};code=BE_Result_Failed;}
    auto body=result.dump();return host.reply(host.context,request,code,body.c_str());
}
void BE_CALL shutdown() {std::lock_guard lock(lifecycle);disable();}
const BE_ThirdPartyModuleV1 api{sizeof(BE_ThirdPartyModuleV1),1,id,initialize,configure,message,shutdown};
}
extern "C" __declspec(dllexport) const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1() {return &api;}
