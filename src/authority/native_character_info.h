#pragma once
// Private native character DATA, not an account grant or a spawned actor.
// No CharBag/friend/gacha factory, inventory mutation, official send or native
// result is used here. The production adapter's certificates remain false.
#include "world_authority.h"
#include "native_object_bindings.h"
#include "native_members.h"

namespace eflink::worldauthority {
class NativeCharacterInfo {
public:
    class Lease {
        friend class NativeCharacterInfo;
        NativePin root;const NativeCharacterInfo* owner=nullptr;
        explicit Lease(NativePin pin,const NativeCharacterInfo* value):root(std::move(pin)),owner(value) {}
    public:
        Lease(const Lease&)=default;Lease& operator=(const Lease&)=default;
    };
    enum class Phase {Empty,Constructing,Ready,Uncertain,Cleaning,Cleaned};
private:
    eflink::unity::Api bridge;const BE_HostApiV1* runtime=nullptr;NativeObjectPins* pins=nullptr;
    const void* profileClass=nullptr,*infoClass=nullptr,*weaponClass=nullptr,*gemClass=nullptr,*charClass=nullptr;
    int mainScope=0;
    BE_ResolvedMethodV1 constructor{},generate{},isRuntime{},getWeapon{},getLevel{},getExp{},getPotential{},getTemplate{},unbind{},detach{},refreshCharacter{},refreshWeapon{},snapshot{};
    NativePin infoRoot;void* info=nullptr;Scope scope_;NativeDomain domain_=NativeDomain::Personal;std::uint16_t peer_=0;std::uint64_t member_=0;unsigned slot_=0;
    bool bound=false,busy=false,cleanupFault=false,cleanupDispatched=false,cleanupObserved=false;std::thread::id owner;Phase phase_=Phase::Empty;
    std::uint64_t constructions=0,cleanups=0;std::string error;
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}};
    void thread() {
        if(owner==std::thread::id{})owner=std::this_thread::get_id();
        if(owner!=std::this_thread::get_id())throw std::runtime_error("native character data outside game thread");
    }
    static bool verified(const NativeAdapter& adapter) {
        return adapter.gameVersion()=="1.5.3"&&adapter.executionVerified()&&adapter.isolationVerified()&&adapter.recoveryVerified()&&adapter.dualClientVerified();
    }
    static const void* klassOf(void* object) {
        auto fn=reinterpret_cast<const void*(*)(void*)>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_object_get_class"));
        if(!object||!fn)throw std::runtime_error("native character data class unavailable");return fn(object);
    }
public:
    NativeCharacterInfo()=default;NativeCharacterInfo(const NativeCharacterInfo&)=delete;NativeCharacterInfo& operator=(const NativeCharacterInfo&)=delete;
    void bind(const BE_HostApiV1* host,const std::string& version,NativeObjectPins& retention) {
        thread();if(busy)throw std::runtime_error("native character data bind during callback");
        if(version!="1.5.3") {bound=false;throw std::runtime_error("native character data version unverified");}
        if(bound&&host==runtime&&pins==&retention)return;
        if(retained())throw std::runtime_error("native character data must retire before runtime replacement");
        bound=false;if(!host||!host->object_new||!host->object_unbox||!host->resolve_method||!host->resolve_class||!host->runtime_invoke||!host->gchandle_new||!host->gchandle_free)
            throw std::runtime_error("native character data runtime unavailable");
        bridge=eflink::unity::Api{};bridge.bind(host);
        profileClass=bridge.klass("SROLE_DATA_CHAR","Beyond.Network.Proto.dll","Proto").class_info;
        infoClass=bridge.klass("ClientCharInfo","Gameplay.Beyond.dll","Beyond.Gameplay").class_info;
        charClass=bridge.klass("CharInfo","Gameplay.Beyond.dll","Beyond.Gameplay").class_info;
        weaponClass=bridge.klass("InventorySystem/WeaponInstData","Gameplay.Beyond.dll","Beyond.Gameplay").class_info;
        gemClass=bridge.klass("InventorySystem/GemInstData","Gameplay.Beyond.dll","Beyond.Gameplay").class_info;
        mainScope=NativeMembers::enumInt(bridge.klass("ScopeName","Common.Beyond.dll","Beyond.GEnums").class_info,"Main");
        constructor=bridge.method("ClientCharInfo",".ctor","Proto.SROLE_DATA_CHAR|Beyond.GEnums.ScopeName","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay");
        generate=bridge.method("GameUtil","GenerateRuntimeClientId","","System.UInt64","Gameplay.Beyond.dll","Beyond.Gameplay");
        isRuntime=bridge.method("GameUtil","IsRuntimeClientId","System.UInt64","System.Boolean","Gameplay.Beyond.dll","Beyond.Gameplay");
        getWeapon=bridge.method("SROLE_DATA_CHAR","get_Weapon","","Proto.WEAPON_DATA","Beyond.Network.Proto.dll","Proto");
        getLevel=bridge.method("SROLE_DATA_CHAR","get_Level","","System.UInt32","Beyond.Network.Proto.dll","Proto");
        getExp=bridge.method("SROLE_DATA_CHAR","get_Exp","","System.UInt32","Beyond.Network.Proto.dll","Proto");
        getPotential=bridge.method("SROLE_DATA_CHAR","get_PotentialLevel","","System.UInt32","Beyond.Network.Proto.dll","Proto");
        getTemplate=bridge.method("SROLE_DATA_CHAR","get_TemplateId","","System.String","Beyond.Network.Proto.dll","Proto");
        unbind=bridge.method("CharInfo","_UnbindDeckAttrChangedNotifySquadInFightEvent","","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay");
        detach=bridge.method("CardSkill","Detach","","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay.Core");
        refreshCharacter=bridge.method("CharInfo","_RefreshCharAttribute","","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay");
        refreshWeapon=bridge.method("CharInfo","_RefreshWeaponAttribute","","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay");
        snapshot=bridge.method("CharInfo","_SnapshotDeckAttr","","System.Void","Gameplay.Beyond.dll","Beyond.Gameplay");
        runtime=host;pins=&retention;bound=true;
    }
    // nativeScope and friendship come from native public gameplay observations,
    // never an account ID or an arbitrary field/action supplied by room JSON.
    std::optional<Lease> construct(const NativeAdapter& adapter,const Scope& scope,NativeDomain domain,std::uint16_t peer,std::uint64_t member,unsigned slot,
                                  void* publicProfile,int nativeScope,int friendship,const std::function<bool()>& current) {
        thread();if(busy||!bound||retained()||!scope.valid()||(domain!=NativeDomain::HostWorld&&domain!=NativeDomain::GuestRoom)||!peer||!member||slot>3||nativeScope!=mainScope||friendship<0||friendship>1000000||
            !publicProfile||!current||!verified(adapter)||(phase_!=Phase::Empty&&phase_!=Phase::Cleaned))return {};
        Guard guard(busy);bool enteredConstructor=false;phase_=Phase::Constructing;cleanupFault=cleanupDispatched=cleanupObserved=false;error.clear();
        auto stillCurrent=[&] {if(!current()||!verified(adapter))throw std::runtime_error("native character construction permission revoked");};
        try {
            stillCurrent();if(klassOf(publicProfile)!=profileClass)throw std::runtime_error("native public character profile type differs");
            eflink::unity::Api::TemporaryRoot sourceRoot(runtime,publicProfile);
            const auto level=bridge.value<std::uint32_t>(getLevel,publicProfile),exp=bridge.value<std::uint32_t>(getExp,publicProfile),potential=bridge.value<std::uint32_t>(getPotential,publicProfile);
            if(!level||level>1000||exp>1000000000||potential>100)throw std::runtime_error("native public character growth outside construction bound");
            auto* templateValue=bridge.call(getTemplate,publicProfile);auto* sourceWeapon=bridge.call(getWeapon,publicProfile);
            if(!templateValue||!sourceWeapon)throw std::runtime_error("native character profile requires observed template and weapon");
            eflink::unity::Api::TemporaryRoot templateRoot(runtime,templateValue),weaponRoot(runtime,sourceWeapon);
            stillCurrent();info=runtime->object_new(runtime->context,infoClass);if(!info)throw std::runtime_error("native private character allocation failed");
            infoRoot=pins->retain(reinterpret_cast<std::uintptr_t>(info));scope_=scope;domain_=domain;peer_=peer;member_=member;slot_=slot;
            stillCurrent();enteredConstructor=true;++constructions;bridge.call(constructor,info,{publicProfile,&nativeScope});stillCurrent();
            NativeMembers fields(bridge);
            if(fields.reference(info,"templateId")!=templateValue||fields.read<int>(info,"level","System.Int32")!=int(level)||
               fields.read<int>(info,"exp","System.Int32")!=int(exp)||fields.read<std::uint32_t>(info,"potentialLevel","System.UInt32")!=potential||
               fields.read<int>(info,"scope","Beyond.GEnums.ScopeName")!=nativeScope)
                throw std::runtime_error("native constructor changed observed character profile; private data not published");
            auto characterId=fields.read<std::uint64_t>(info,"instId","System.UInt64");
            if(!characterId||!bridge.value<bool>(isRuntime,nullptr,{&characterId}))throw std::runtime_error("native character did not receive a runtime client identity");
            auto* weapon=fields.reference(info,"m_weaponData",weaponClass);auto* gem=fields.reference(info,"m_gemInstData",gemClass);
            if(!weapon)throw std::runtime_error("native constructor did not retain private weapon data");
            std::set<std::uint64_t> ids{characterId};
            auto newId=[&] {auto id=bridge.value<std::uint64_t>(generate);stillCurrent();
                if(!id||!ids.insert(id).second||!bridge.value<bool>(isRuntime,nullptr,{&id}))throw std::runtime_error("native private equipment identity unavailable");stillCurrent();return id;};
            const auto weaponId=newId();const auto gemId=gem?newId():0;stillCurrent();
            // Only the new ClientCharInfo and its constructor-owned data are
            // initialized. No source proto, CharBag or official item is changed.
            fields.initialize(info,"weaponInstId","System.UInt64",weaponId);fields.initialize(weapon,"instId","System.UInt64",weaponId);
            fields.initialize(weapon,"equippedCharServerId","System.UInt64",characterId);fields.initialize(weapon,"attachedGemInstId","System.UInt64",gemId);
            if(gem) {fields.initialize(gem,"instId","System.UInt64",gemId);fields.initialize(gem,"weaponInstId","System.UInt64",weaponId);}
            fields.initialize(info,"friendship","System.Int32",friendship);stillCurrent();
            // The constructor already initialized attributes. Recompute from
            // these now-local references through native systems, never direct
            // HP/stat writes or a second InitDeckData with duplicate skills.
            bridge.call(refreshCharacter,info);stillCurrent();bridge.call(refreshWeapon,info);stillCurrent();bridge.call(snapshot,info);stillCurrent();
            phase_=Phase::Ready;return Lease(infoRoot,this);
        }catch(const std::exception& e) {error=e.what();}catch(...) {error="native private character construction failed";}
        if(enteredConstructor)phase_=Phase::Uncertain;
        else {infoRoot.reset();info=nullptr;phase_=Phase::Empty;}
        return {};
    }
    template<class Use> bool withInfo(const Lease& lease,const Scope& scope,NativeDomain domain,std::uint16_t peer,std::uint64_t member,unsigned slot,Use use) const {
        if(owner!=std::this_thread::get_id()||busy||!bound||phase_!=Phase::Ready||lease.owner!=this||lease.root!=infoRoot||
           scope!=scope_||domain!=domain_||peer!=peer_||member!=member_||slot!=slot_||!pins->matches(infoRoot,reinterpret_cast<std::uintptr_t>(info)))return false;
        const auto retained=infoRoot;return use(info);
    }
    bool cleanup(const std::function<bool(void*)>& observed={}) {
        thread();if(busy)return false;if(!retained())return true;
        if(!bound||cleanupFault||infoRoot.use_count()!=1||!pins->matches(infoRoot,reinterpret_cast<std::uintptr_t>(info)))return false;
        Guard guard(busy);phase_=Phase::Cleaning;NativeMembers fields(bridge);
        try {
            if(!cleanupDispatched) {
                ++cleanups;auto* skills=fields.reference(info,"cardSkills");
                if(skills) {
                    const auto skillSnapshot=fields.references(skills,"Beyond.Gameplay.Core.CardSkill",*pins);
                    for(const auto& skill:skillSnapshot) {
                        if(fields.reference(skill.object,"m_charInfo",charClass)!=info)throw std::runtime_error("private character references a foreign card skill");
                        bridge.call(detach,skill.object);
                    }
                    if(fields.count(skills)!=0)throw std::runtime_error("private card skills still attached after cleanup");
                }
                bridge.call(unbind,info);
                if(fields.read<bool>(info,"m_isDeckAttrChangedNotifySquadInFightEventBound","System.Boolean"))throw std::runtime_error("private character native event subscription remains");
                cleanupDispatched=true;
            }
            // _Unbind sets its flag before removing the actual event delegate.
            // Require an independent native registry/modifier observation; a
            // false field and a successful return cannot establish cleanup.
            if(!observed||!observed(info))return false;cleanupObserved=true;
            infoRoot.reset();info=nullptr;phase_=Phase::Cleaned;return true;
        }catch(const std::exception& e) {error=e.what();}catch(...) {error="private native character cleanup uncertain";}
        // Unbind/remove can throw after partial mutation. A false flag or an
        // empty HashSet on the next poll is insufficient to prove restoration.
        cleanupFault=true;phase_=Phase::Uncertain;return false;
    }
    bool retained() const {return bool(infoRoot);}
    Json status() const {
        const char* label=phase_==Phase::Empty?"empty":phase_==Phase::Constructing?"constructing":phase_==Phase::Ready?"data_ready":phase_==Phase::Cleaning?"cleaning":phase_==Phase::Cleaned?"cleaned":"uncertain";
        return {{"phase",label},{"retained",retained()},{"construction_calls",constructions},{"cleanup_calls",cleanups},{"cleanup_uncertain",cleanupFault},
            {"cleanup_dispatched",cleanupDispatched},{"native_cleanup_observed",cleanupObserved},{"error",error},
            {"native_actor_created",false},{"official_account_modified",false},{"execution_verified",false},{"full_sync_verified",false}};
    }
};
}
