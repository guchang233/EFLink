#pragma once
// Native call implementation, not a capability certificate. The ordinary module
// has no verified execution/isolation/recovery certificate and cannot obtain a
// Host dispatch permit. This entry never manufactures a Witness or world result.
#include "native_object_bindings.h"
#include "../unity_bridge.h"

namespace eflink::worldauthority {
class NativeMineEntry {
    eflink::unity::Api bridge;
    const BE_HostApiV1* runtime=nullptr;
    BE_ResolvedMethodV1 operation{},resourceId{};
    int mining=0;
    bool resolved=false;
    std::uint64_t dispatched=0,accepted=0;
    template<class T> static T symbol(const char* name) {
        auto value=reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),name));
        if(!value)throw std::runtime_error("native mining metadata export unavailable");return value;
    }
public:
    static constexpr const char* game="1.5.3";
    static constexpr const char* path="endfield153/native-mine-candidate";
    void bind(const BE_HostApiV1* host,const std::string& version) {
        if(version!=game) {resolved=false;throw std::runtime_error("unverified game version; native mining disabled");}
        if(resolved&&runtime==host)return;
        resolved=false;
        if(!host||!host->object_new||!host->object_unbox||!host->runtime_invoke||!host->resolve_class||!host->resolve_method||
           !host->gchandle_new||!host->gchandle_free)throw std::runtime_error("native mining runtime unavailable");
        bridge=eflink::unity::Api{};bridge.bind(host);
        const auto method=bridge.method("NatureResourceManager","DoOperation",
            "Beyond.ObjectPtr<Beyond.Gameplay.Core.Entity>|Beyond.Gameplay.ENatureOperation|System.UInt64","System.Boolean","Gameplay.Beyond.dll","Beyond.Gameplay");
        const auto getter=bridge.virtualGetter("INatureResource","get_natureResourceId","System.UInt64");
        const auto type=bridge.klass("ENatureOperation","Gameplay.Beyond.dll","Beyond.Gameplay");
        authority::requireNativeValue<int>(eflink::unity::Api::valueShape(type.class_info));
        auto field=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(type.class_info,"Mining");
        if(!field||!(symbol<int(*)(const void*)>("il2cpp_field_get_flags")(field)&0x10)||
           symbol<const void*(*)(const void*)>("il2cpp_class_from_type")(symbol<const void*(*)(const void*)>("il2cpp_field_get_type")(field))!=type.class_info)
            throw std::runtime_error("native Mining enum contract differs");
        int value=0;symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(field,&value);
        operation=method;resourceId=getter;mining=value;runtime=host;resolved=true;
    }
    bool invoke(const NativeAdapter& adapter,const Ticket& ticket,const NativeDispatchPermit& permit,const NativeBinding& actor,const NativeBinding& target,void* manager) {
        if(!resolved||!manager||!permit.issuedTo(&adapter)||!permit.allows(ticket,path)||ticket.intent.operation!="mine"||!ticket.intent.input.empty()||
           actor.kind!="player"||target.kind!="mineral"||actor.cookie!=ticket.actorCookie||target.cookie!=ticket.targetCookie||
           actor.domain!=NativeDomain::HostWorld||target.domain!=NativeDomain::HostWorld||!actor.allocation.valid()||!target.allocation.valid())return false;
        auto* nativeActor=reinterpret_cast<void*>(actor.allocation.address);auto* nativeTarget=reinterpret_cast<void*>(target.allocation.address);
        eflink::unity::Api::TemporaryRoot actorRoot(runtime,nativeActor),targetRoot(runtime,nativeTarget),managerRoot(runtime,manager);
        auto id=bridge.virtualValue<std::uint64_t>(resourceId,nativeTarget);
        if(!id||id!=target.allocation.secondaryId)return false;
        auto* boxed=bridge.parameterValue(operation,0);eflink::unity::Api::TemporaryRoot argumentRoot(runtime,boxed);
        bridge.constructReferenceValue(boxed,nativeActor,"Beyond.Gameplay.Core.Entity");
        auto* argument=runtime->object_unbox(runtime->context,boxed);if(!argument||!permit.consume(ticket,path))return false;
        // DoOperation validates and executes through the game's own operation/
        // skill systems. Returning true means dispatch accepted, never ore hit,
        // durability changed, collected, official reward, or synchronization.
        ++dispatched;
        const bool result=bridge.value<bool>(operation,manager,{argument,&mining,&id});
        if(result)++accepted;return result;
    }
    bool contractsResolved() const {return resolved;}
    std::uint64_t calls() const {return dispatched;}
    std::uint64_t acceptedCalls() const {return accepted;}
};
}
