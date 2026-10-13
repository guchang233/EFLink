// A one-shot, read-only resource inspector. Calls occur on the game's own tick,
// rather than a worker racing mutable collections. No room switch, native
// operation, field write, input event or account/network request is performed.
#include "../link_world.h"
#include "../runtime_scope.h"
#include "BetterEndfield/ThirdPartyModule.h"
#include <atomic>
#include <mutex>
namespace {
using nlohmann::json;
#ifndef EFLINK_RESOURCE_OBSERVER_ID
#define EFLINK_RESOURCE_OBSERVER_ID "eflink.coop.resourceprobe1"
#endif
constexpr char id[]=EFLINK_RESOURCE_OBSERVER_ID;
BE_ThirdPartyHostV1 host{};
std::mutex mutex;bool initialized=false,hooked=false;
std::atomic<bool> requested=false;
json captureResult;
BE_ResolvedMethodV1 tickMethod{};const void* cameraClass=nullptr;
using ClassOf=const void*(*)(void*);using ParentOf=const void*(*)(const void*);
ClassOf classOf=nullptr;ParentOf parentOf=nullptr;
using Tick=void(__fastcall*)(void*,float,void*);Tick nextTick=nullptr;
const BE_HostApiV1* runtime(){return host.get_runtime?host.get_runtime(host.context):host.runtime;}
bool camera(void* object) noexcept {
    __try {for(auto* c=classOf(object);c;c=parentOf(c))if(c==cameraClass)return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}return false;
}
void __fastcall tick(void* self,float delta,void* info) {
    if(nextTick)nextTick(self,delta,info);
    if(!self||(info&&info!=tickMethod.method_info)||!camera(self)||!requested.exchange(false))return;
    std::lock_guard lock(mutex);if(!initialized)return;
    try {eflink::World inspector;captureResult=inspector.inspectNativeResources(runtime());}
    catch(const std::exception& e){captureResult={{"error",e.what()},{"gameplay_modified",false}};}
    catch(...){captureResult={{"error","native resource inspector fault"},{"gameplay_modified",false}};}
}
void install() {
    if(hooked)return;
    RuntimeThreadScope attached(true);auto* api=runtime();auto dll=GetModuleHandleW(L"GameAssembly.dll");
    if(!attached.ready()||!api||!dll||!host.hooks||!host.hooks->create||!host.hooks->disable_module)throw std::runtime_error("resource observer runtime unavailable");
    classOf=reinterpret_cast<ClassOf>(GetProcAddress(dll,"il2cpp_object_get_class"));
    parentOf=reinterpret_cast<ParentOf>(GetProcAddress(dll,"il2cpp_class_get_parent"));
    BE_ResolvedClassV1 klass{};
    if(!classOf||!parentOf||api->resolve_class(api->context,"Gameplay.Beyond.dll","Beyond.Gameplay.View","CameraManager",&klass)!=BE_Result_Ok)
        throw std::runtime_error("resource observer camera class unavailable");
    cameraClass=klass.class_info;
    BE_MethodDescriptorV1 method{"Gameplay.Beyond.dll","Beyond.Gameplay.View","CameraManager","TailLateTick","System.Single","System.Void",1};
    if(api->resolve_method(api->context,&method,&tickMethod)!=BE_Result_Ok||!tickMethod.method_pointer)throw std::runtime_error("resource observer game tick unavailable");
    std::uint64_t handle=0;
    if(host.hooks->create(host.hooks->context,id,tickMethod.method_pointer,reinterpret_cast<void*>(tick),reinterpret_cast<void**>(&nextTick),&handle)!=BE_Result_Ok)
        throw std::runtime_error("resource observer shared hook failed");
    hooked=true;
}
BE_Result BE_CALL init(const BE_ThirdPartyHostV1* input,const char*) {
    std::lock_guard lock(mutex);
    if(!input||input->version!=1||!input->module_id||std::string(input->module_id)!=id||!input->reply||
       input->struct_size<offsetof(BE_ThirdPartyHostV1,emit)+sizeof(input->emit))return BE_Result_ContractMismatch;
    host={};std::memcpy(&host,input,(std::min)(std::size_t(input->struct_size),sizeof(host)));initialized=true;return BE_Result_Ok;
}
BE_Result BE_CALL configure(const char*){return BE_Result_Ok;}
BE_Result BE_CALL message(const char* request,const char* body) {
    std::lock_guard lock(mutex);if(!initialized)return BE_Result_NotReady;
    json output;BE_Result code=BE_Result_Ok;
    try {
        const auto command=json::parse(body);const auto action=command.at("action").get<std::string>();
        if(action=="capture") {install();captureResult=nullptr;requested=true;output={{"queued",true},{"gameplay_modified",false}};}
        else if(action=="status")output={{"pending",requested.load()},{"result",captureResult},{"gameplay_modified",false}};
        else throw std::runtime_error("unknown resource observer action");
    }catch(const std::exception& e){code=BE_Result_InvalidArgument;output={{"error",e.what()}};}
    const auto text=output.dump();return host.reply(host.context,request,code,text.c_str());
}
void BE_CALL stop() {
    requested=false;
    // Disable outside the inspector lock: a running tick may need that lock.
    if(host.hooks&&host.hooks->disable_module)host.hooks->disable_module(host.hooks->context,id);
    std::lock_guard lock(mutex);initialized=false;hooked=false;captureResult=nullptr;
}
const BE_ThirdPartyModuleV1 module{sizeof(BE_ThirdPartyModuleV1),1,id,init,configure,message,stop};
}
extern "C" __declspec(dllexport) const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1(){return &module;}
