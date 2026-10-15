#pragma once
// A native entity-storage withdrawal lease, NOT full personal-world isolation.
// The 1.5.3 mask covers Interactive/Enemy/Npc only. It does not certify character,
// skill, task, outbound-account or official message isolation and never restores
// account values from an old snapshot. No raw handle is accepted from JSON.
#include "native_object_pins.h"
#include "world_authority.h"
#include "../unity_bridge.h"

namespace eflink::worldauthority {
class NativeEntityWithdrawal {
public:
    struct StorageObservation {std::uintptr_t storage=0;std::uint32_t counter=0;bool handlePresent=false;};
    using Observe=std::function<StorageObservation(void*,std::uint32_t)>;
    enum class Phase {Idle,Acquiring,Active,Retiring,Retired,Uncertain};
private:
    eflink::unity::Api bridge;const BE_HostApiV1* runtime=nullptr;NativeObjectPins* roots=nullptr;
    BE_ResolvedMethodV1 hide{},revert{};int entities=0,npcs=0;bool bound=false;
    NativePin managerRoot,storageRoot;std::uintptr_t manager=0,storage=0;std::uint32_t handle=0,counter=0;
    Phase phase_=Phase::Idle;std::thread::id owner;std::string error;
    bool busy=false;
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}};
    std::uint64_t acquisitions=0,reversions=0;
    template<class T> static T symbol(const char* name) {
        auto* module=GetModuleHandleW(L"GameAssembly.dll");auto fn=reinterpret_cast<T>(GetProcAddress(module,name));
        if(!fn)throw std::runtime_error("native entity withdrawal metadata unavailable");return fn;
    }
    void thread() {
        if(owner==std::thread::id{})owner=std::this_thread::get_id();
        if(owner!=std::this_thread::get_id())throw std::runtime_error("native entity lease outside game thread");
    }
    int mask(const char* type,std::initializer_list<const char*> names) {
        auto klass=bridge.klass(type,"Gameplay.Beyond.dll","Beyond.Gameplay.Core");
        authority::requireNativeValue<int>(eflink::unity::Api::valueShape(klass.class_info));int result=0;
        for(const auto* name:names) {
            const auto* field=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,name);
            if(!field||!(symbol<int(*)(const void*)>("il2cpp_field_get_flags")(field)&0x10)||
               symbol<const void*(*)(const void*)>("il2cpp_class_from_type")(symbol<const void*(*)(const void*)>("il2cpp_field_get_type")(field))!=klass.class_info)
                throw std::runtime_error("native withdrawal enum contract differs");
            int value=0;symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(field,&value);
            if(value<=0)throw std::runtime_error("native withdrawal enum value unavailable");result|=value;
        }
        return result;
    }
    bool current(void* currentManager,const StorageObservation& observation) const {
        return manager==reinterpret_cast<std::uintptr_t>(currentManager)&&observation.storage==storage&&storage&&observation.counter>=counter;
    }
public:
    NativeEntityWithdrawal()=default;NativeEntityWithdrawal(const NativeEntityWithdrawal&)=delete;
    NativeEntityWithdrawal& operator=(const NativeEntityWithdrawal&)=delete;
    void bind(const BE_HostApiV1* host,const std::string& version,NativeObjectPins& retention) {
        thread();if(busy)throw std::runtime_error("cannot replace native entity lease runtime during a callback");
        if(version!="1.5.3") {bound=false;throw std::runtime_error("unverified native entity withdrawal version");}
        if(bound&&runtime==host&&roots==&retention)return;
        if(retained())throw std::runtime_error("cannot replace a retained native entity lease runtime");
        bound=false;if(!host||!host->resolve_class||!host->resolve_method||!host->runtime_invoke||!host->object_unbox||!host->gchandle_new||!host->gchandle_free)
            throw std::runtime_error("native entity withdrawal runtime unavailable");
        bridge=eflink::unity::Api{};bridge.bind(host);
        const auto hideMethod=bridge.method("EntityManager","HideAllTypeOfEntity",
            "Beyond.Gameplay.Core.HideEntityTypeMask|System.Collections.Generic.HashSet<System.UInt64>|Beyond.Gameplay.Core.HideNpcTypeMask|System.Collections.Generic.HashSet<System.String>",
            "System.UInt32","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
        const auto revertMethod=bridge.method("EntityManager","RevertHideAllTypeOfEntity","System.UInt32","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
        const auto entityMask=mask("HideEntityTypeMask",{"Interactive","Enemy","Npc"});
        const auto npcMask=mask("HideNpcTypeMask",{"Atmospheric","Gameplay","Proxy","Environmental"});
        runtime=host;roots=&retention;hide=hideMethod;revert=revertMethod;entities=entityMask;npcs=npcMask;bound=true;
    }
    bool acquire(const NativeAdapter& adapter,void* currentManager,const Observe& observe) {
        thread();if(busy||!bound||(phase_!=Phase::Idle&&phase_!=Phase::Retired)||!currentManager||!observe||adapter.gameVersion()!="1.5.3"||
            !adapter.isolationVerified()||!adapter.recoveryVerified()||!adapter.dualClientVerified())return false;
        Guard guard(busy);handle=counter=0;manager=storage=0;error.clear();
        // Even observation/GC calls can re-enter. Close admission before them,
        // hold the manager first, and restore Idle only before any mutation.
        phase_=Phase::Acquiring;bool dispatched=false;
        try {
            managerRoot=roots->retain(reinterpret_cast<std::uintptr_t>(currentManager));
            const auto initial=observe(currentManager,0);
            if(!initial.storage||adapter.gameVersion()!="1.5.3"||!adapter.isolationVerified()||!adapter.recoveryVerified()||!adapter.dualClientVerified())
                throw std::runtime_error("personal entity lease admission changed before native mutation");
            manager=reinterpret_cast<std::uintptr_t>(currentManager);storage=initial.storage;counter=initial.counter;
            storageRoot=roots->retain(storage);
            const auto retained=observe(currentManager,0);
            if(!current(currentManager,retained)||adapter.gameVersion()!="1.5.3"||!adapter.isolationVerified()||!adapter.recoveryVerified()||!adapter.dualClientVerified())
                throw std::runtime_error("personal entity storage changed while retaining its native lease");
            counter=retained.counter;
            // A thrown call could have acquired a handle without returning it.
            // The retained barrier remains in that case; never retry blindly.
            dispatched=true;++acquisitions;
            // Both optional whitelists are intentionally null, meaning no
            // exempt entities. Values are native enum fields, never constants.
            handle=bridge.value<std::uint32_t>(hide,currentManager,{&entities,nullptr,&npcs,nullptr});
            if(!handle)throw std::runtime_error("native withdrawal returned no owned handle");
            const auto observed=observe(currentManager,handle);
            if(!current(currentManager,observed)||!observed.handlePresent)throw std::runtime_error("native withdrawal handle was not observed in its original storage");
            counter=observed.counter;phase_=Phase::Active;return true;
        }catch(const std::exception& e) {error=e.what();phase_=dispatched?Phase::Uncertain:Phase::Idle;if(!dispatched) {managerRoot.reset();storageRoot.reset();}return false;}
        catch(...) {error="native withdrawal outcome uncertain";phase_=dispatched?Phase::Uncertain:Phase::Idle;if(!dispatched) {managerRoot.reset();storageRoot.reset();}return false;}
    }
    bool retire(void* currentManager,const Observe& observe) {
        thread();if(busy)return false;if(phase_==Phase::Retired||phase_==Phase::Idle)return true;
        if(!bound||!currentManager||!observe||!handle)return false;
        Guard guard(busy);
        try {
            auto observed=observe(currentManager,handle);
            if(!current(currentManager,observed))throw std::runtime_error("personal entity storage changed; retained lease cannot act on a different world");
            if(phase_==Phase::Active) {
                if(!observed.handlePresent)throw std::runtime_error("owned native withdrawal handle disappeared before restoration");
                phase_=Phase::Retiring;++reversions;
                bridge.call(revert,currentManager,{&handle});observed=observe(currentManager,handle);
            }
            // Function return alone is never proof of retirement. A failed or
            // asynchronous revert is observed again but never called twice.
            if(!current(currentManager,observed)||observed.handlePresent)return false;
            phase_=Phase::Retired;managerRoot.reset();storageRoot.reset();return true;
        }catch(const std::exception& e) {error=e.what();phase_=Phase::Uncertain;return false;}
        catch(...) {error="native entity lease retirement uncertain";phase_=Phase::Uncertain;return false;}
    }
    bool retained() const {return bool(managerRoot)||bool(storageRoot);}
    Phase phase() const {return phase_;}
    Json status() const {
        const char* label=phase_==Phase::Idle?"idle":phase_==Phase::Acquiring?"acquiring":phase_==Phase::Active?"active":phase_==Phase::Retiring?"retiring":phase_==Phase::Retired?"retired":"uncertain";
        return {{"phase",label},{"retained",retained()},{"acquire_calls",acquisitions},{"revert_calls",reversions},{"error",error},
            {"native_entity_slice_only",true},{"character_isolation",false},{"official_reporting_isolation",false},
            {"personal_world_restored",false},{"isolation_verified",false},{"full_sync_verified",false}};
    }
};
}
