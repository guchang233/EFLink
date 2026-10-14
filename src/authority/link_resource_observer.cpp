// A one-shot, read-only resource inspector. Calls occur on the game's own tick,
// rather than a worker racing mutable collections. No room switch, native
// operation, field write, input event or account/network request is performed.
#include "../link_world.h"
#include "../runtime_scope.h"
#include "native_trace_window.h"
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
eflink::worldauthority::NativeTraceWindow traceWindow;bool attackHooked=false,attackAttempted=false;std::uint64_t traceSequence=0;
json attackTrace=json::array();std::string observedVersion;std::atomic<std::uint64_t> traceFaults{0};
BE_ResolvedMethodV1 attackMethod{};
using Attack=void(__fastcall*)(void*,void*,void*);Attack nextAttack=nullptr;
BE_ResolvedMethodV1 receiveMethod{},runtimeHpMethod{},serverHpMethod{};
using Receive=void(__fastcall*)(void*,void*,void*,void*);Receive nextReceive=nullptr;
using RuntimeHp=void(__fastcall*)(void*,void*);RuntimeHp nextRuntimeHp=nullptr;
using ServerHp=void(__fastcall*)(void*,void*,int,int,void*);ServerHp nextServerHp=nullptr;
BE_ResolvedMethodV1 tickMethod{};const void* cameraClass=nullptr;
using ClassOf=const void*(*)(void*);using ParentOf=const void*(*)(const void*);
ClassOf classOf=nullptr;ParentOf parentOf=nullptr;
using Tick=void(__fastcall*)(void*,float,void*);Tick nextTick=nullptr;
const BE_HostApiV1* runtime(){return host.get_runtime?host.get_runtime(host.context):host.runtime;}
bool camera(void* object) noexcept {
    __try {for(auto* c=classOf(object);c;c=parentOf(c))if(c==cameraClass)return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}return false;
}
json observeAttack(void* self,void* board) noexcept {
    try {eflink::World inspector;return inspector.inspectNativeMineralAttack(runtime(),self,board);}
    catch(...) {++traceFaults;return nullptr;}
}
bool observing(void* self,void* info,const BE_ResolvedMethodV1& method) {
    return traceWindow.active(eflink::nowMs())&&self&&(!info||info==method.method_info)&&
        (!traceWindow.awaitingAttack()||&method==&attackMethod||&method==&receiveMethod);
}
void appendTrace(const char* phase,json before,json after) noexcept {
    try {
        std::lock_guard lock(mutex);if(!initialized||!traceWindow.active(eflink::nowMs()))return;
        attackTrace.push_back({{"phase",phase},{"sequence",++traceSequence},{"at",eflink::nowMs()},{"before",std::move(before)},{"after",std::move(after)}});
        if(attackTrace.size()>64)attackTrace.erase(attackTrace.begin());
    }catch(...) {++traceFaults;}
}
void __fastcall attack(void* self,void* board,void* info) {
    // Every original call proceeds exactly once. The probe never gates actions,
    // mutates fields, invokes attacks or sends official/room network requests.
    const bool active=observing(self,info,attackMethod);
    json before;if(active)before=observeAttack(self,board);
    if(active&&before.is_object()&&before.contains("key"))traceWindow.nativeAttack(eflink::nowMs());
    if(nextAttack)nextAttack(self,board,info);
    if(!active)return;
    auto after=observeAttack(self,nullptr);
    appendTrace("int_receive",std::move(before),std::move(after));
}
void __fastcall receive(void* self,void* board,void* state,void* info) {
    const bool active=observing(self,info,receiveMethod);json before;if(active)before=observeAttack(self,board);
    if(active&&before.is_object()&&before.contains("key"))traceWindow.nativeAttack(eflink::nowMs());
    if(nextReceive)nextReceive(self,board,state,info);
    if(active)appendTrace("receive",std::move(before),observeAttack(self,nullptr));
}
void __fastcall runtimeHp(void* self,void* info) {
    const bool active=observing(self,info,runtimeHpMethod);json before;if(active)before=observeAttack(self,nullptr);
    if(nextRuntimeHp)nextRuntimeHp(self,info);
    if(active)appendTrace("runtime_hp",std::move(before),observeAttack(self,nullptr));
}
void __fastcall serverHp(void* self,void* name,int oldValue,int newValue,void* info) {
    const bool active=observing(self,info,serverHpMethod);json before;if(active)before=observeAttack(self,nullptr);
    if(nextServerHp)nextServerHp(self,name,oldValue,newValue,info);
    if(active)appendTrace("server_hp",std::move(before),observeAttack(self,nullptr));
}
void verifyVersion() {
    observedVersion=eflink::worldauthority::nativeGameVersion(runtime());
    if(!eflink::worldauthority::readOnlyBuildSupported(observedVersion))throw std::runtime_error("read-only mineral probe disabled for unverified game version");
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
    verifyVersion();
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
void installAttack() {
    if(attackHooked)return;
    if(attackAttempted)throw std::runtime_error("incomplete observation installation; disable this probe before retrying");
    install();RuntimeThreadScope attached(true);auto* api=runtime();
    if(!attached.ready())throw std::runtime_error("mineral event runtime attachment failed");
    BE_MethodDescriptorV1 descriptor{"Gameplay.Beyond.dll","Beyond.Gameplay.Core","InteractiveDoodadCommonComponent","_OnIntReceiveAttack","Beyond.Gameplay.ParamBlackboard","System.Void",1};
    if(api->resolve_method(api->context,&descriptor,&attackMethod)!=BE_Result_Ok||!attackMethod.method_pointer)throw std::runtime_error("exact native mineral attack signature unavailable");
    attackAttempted=true;std::uint64_t handle=0;
    if(host.hooks->create(host.hooks->context,id,attackMethod.method_pointer,reinterpret_cast<void*>(attack),reinterpret_cast<void**>(&nextAttack),&handle)!=BE_Result_Ok)
        throw std::runtime_error("mineral event shared hook failed");
    // PropertyStateData is a managed reference in the inspected build; do not
    // guess an ABI if a later client changes it to a value type.
    const auto dll=GetModuleHandleW(L"GameAssembly.dll");
    auto valueType=reinterpret_cast<bool(*)(const void*)>(GetProcAddress(dll,"il2cpp_class_is_valuetype"));
    BE_ResolvedClassV1 state{};
    if(!valueType||api->resolve_class(api->context,"Gameplay.Beyond.dll","Beyond.Gameplay.Core","PropertyStateData",&state)!=BE_Result_Ok||!state.class_info||valueType(state.class_info))
        throw std::runtime_error("unverified mineral receive callback ABI");
    auto hook=[&](const char* name,const char* parameters,unsigned count,BE_ResolvedMethodV1& method,void* callback,void** next) {
        BE_MethodDescriptorV1 d{"Gameplay.Beyond.dll","Beyond.Gameplay.Core","InteractiveDoodadCommonComponent",name,parameters,"System.Void",count};
        if(api->resolve_method(api->context,&d,&method)!=BE_Result_Ok||!method.method_pointer)throw std::runtime_error(std::string("native observation signature unavailable: ")+name);
        std::uint64_t hookHandle=0;
        if(host.hooks->create(host.hooks->context,id,method.method_pointer,callback,next,&hookHandle)!=BE_Result_Ok)throw std::runtime_error(std::string("native observation hook failed: ")+name);
    };
    hook("OnReceiveAttack","Beyond.Gameplay.ParamBlackboard|Beyond.Gameplay.Core.PropertyStateData",2,receiveMethod,reinterpret_cast<void*>(receive),reinterpret_cast<void**>(&nextReceive));
    hook("_OnHPRatioRuntimeChanged",nullptr,0,runtimeHpMethod,reinterpret_cast<void*>(runtimeHp),reinterpret_cast<void**>(&nextRuntimeHp));
    hook("_OnHpRatioPropertyChanged","System.String|System.Int32|System.Int32",3,serverHpMethod,reinterpret_cast<void*>(serverHp),reinterpret_cast<void**>(&nextServerHp));
    attackHooked=true;
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
        else if(action=="trace") {
            const auto duration=command.value("duration_ms",600000u);if(duration<1000||duration>600000)throw std::runtime_error("trace duration outside limits");
            installAttack();attackTrace=json::array();traceSequence=0;traceFaults=0;
            if(command.value("one_shot",false))traceWindow.singleAttack();else traceWindow.timed(eflink::nowMs(),duration);
            output={{"observing",true},{"awaiting_attack",traceWindow.awaitingAttack()},{"duration_ms",duration},{"gameplay_modified",false},{"execution_adapter_verified",false}};}
        else if(action=="stop_trace") {traceWindow.stop();output={{"observing",false},{"gameplay_modified",false}};}
        else if(action=="status")output={{"pending",requested.load()},{"result",captureResult},{"attack_trace",attackTrace},{"observing",traceWindow.active(eflink::nowMs())},{"awaiting_attack",traceWindow.awaitingAttack()},
            {"game_version",observedVersion},{"trace_faults",traceFaults.load()},{"gameplay_modified",false},{"execution_adapter_verified",false},{"full_sync_verified",false}};
        else throw std::runtime_error("unknown resource observer action");
    }catch(const std::exception& e){code=BE_Result_InvalidArgument;output={{"error",e.what()}};}
    const auto text=output.dump();return host.reply(host.context,request,code,text.c_str());
}
void BE_CALL stop() {
    requested=false;traceWindow.stop();
    // Disable outside the inspector lock: a running tick may need that lock.
    if(host.hooks&&host.hooks->disable_module)host.hooks->disable_module(host.hooks->context,id);
    std::lock_guard lock(mutex);initialized=false;hooked=attackHooked=attackAttempted=false;captureResult=nullptr;attackTrace=json::array();
}
const BE_ThirdPartyModuleV1 module{sizeof(BE_ThirdPartyModuleV1),1,id,init,configure,message,stop};
}
extern "C" __declspec(dllexport) const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1(){return &module;}
