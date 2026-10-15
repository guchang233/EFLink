// Production native lease entry, using test-owned metadata/runtime only.
#include "authority/native_entity_withdrawal.h"
#include "support/native_abi_fixture.h"
#include <iostream>
#include <set>
using namespace eflink::worldauthority;
static void check(bool ok,const char* label) {if(!ok)throw std::runtime_error(label);std::cout<<"PASS "<<label<<'\n';}
struct Adapter:NativeAdapter {
    bool verified=true;std::string gameVersion() const override {return "1.5.3";}
    std::string path() const override {return "fixture/native-entity-slice";}
    bool isolationVerified() const override {return verified;}
    bool recoveryVerified() const override {return verified;}
    bool dualClientVerified() const override {return verified;}
};
struct Runtime {
    AbiFixtureClass entityMask{true,4,4,nullptr},npcMask{true,4,4,nullptr},managerClass{false,0,0,nullptr},uintClass{true,4,4,nullptr};
    AbiFixtureField entityNpc{"Npc",{"Beyond.Gameplay.Core.HideEntityTypeMask",&entityMask},64},
        entityEnemy{"Enemy",entityNpc.type,16,0x10,&entityNpc},entityInteractive{"Interactive",entityNpc.type,4,0x10,&entityEnemy},
        npcEnvironmental{"Environmental",{"Beyond.Gameplay.Core.HideNpcTypeMask",&npcMask},128},
        npcProxy{"Proxy",npcEnvironmental.type,32,0x10,&npcEnvironmental},npcGameplay{"Gameplay",npcEnvironmental.type,8,0x10,&npcProxy},
        npcAtmospheric{"Atmospheric",npcEnvironmental.type,2,0x10,&npcGameplay};
    AbiFixtureMethod hide{&managerClass,nullptr,0,4},revert{&managerClass,nullptr,0,1};
    AbiFixtureObject manager{&managerClass,{}},result{&uintClass,{}};
    std::uint32_t counter=10,owned=0;std::uintptr_t storage=991;
    std::set<unsigned> roots;unsigned next=0,hideCalls=0,revertCalls=0,invalid=0;int entityBits=0,npcBits=0;
    bool deferred=false,hideThrows=false,revertThrows=false,noHandle=false;
    Runtime() {entityMask.field=&entityInteractive;npcMask.field=&npcAtmospheric;}
    static BE_Result BE_CALL klass(void* context,const char*,const char*,const char* name,BE_ResolvedClassV1* out) {
        auto& self=*static_cast<Runtime*>(context);*out={};
        if(!std::strcmp(name,"HideEntityTypeMask"))out->class_info=&self.entityMask;
        if(!std::strcmp(name,"HideNpcTypeMask"))out->class_info=&self.npcMask;
        return out->class_info?BE_Result_Ok:BE_Result_NotFound;
    }
    static BE_Result BE_CALL method(void* context,const BE_MethodDescriptorV1* d,BE_ResolvedMethodV1* out) {
        auto& self=*static_cast<Runtime*>(context);*out={};
        if(std::strcmp(d->assembly_name,"Gameplay.Beyond.dll")||std::strcmp(d->namespace_name,"Beyond.Gameplay.Core")||std::strcmp(d->class_name,"EntityManager"))return BE_Result_NotFound;
        if(!std::strcmp(d->method_name,"HideAllTypeOfEntity")&&d->parameter_count==4&&!std::strcmp(d->return_type,"System.UInt32")&&
           !std::strcmp(d->parameter_types,"Beyond.Gameplay.Core.HideEntityTypeMask|System.Collections.Generic.HashSet<System.UInt64>|Beyond.Gameplay.Core.HideNpcTypeMask|System.Collections.Generic.HashSet<System.String>"))out->method_info=&self.hide;
        if(!std::strcmp(d->method_name,"RevertHideAllTypeOfEntity")&&d->parameter_count==1&&!std::strcmp(d->return_type,"System.Void")&&!std::strcmp(d->parameter_types,"System.UInt32"))out->method_info=&self.revert;
        return out->method_info?BE_Result_Ok:BE_Result_NotFound;
    }
    static unsigned BE_CALL root(void* context,void* object,int) {if(!object)return 0;auto& self=*static_cast<Runtime*>(context);auto handle=++self.next;self.roots.insert(handle);return handle;}
    static void BE_CALL free(void* context,unsigned handle) {auto& self=*static_cast<Runtime*>(context);if(!self.roots.erase(handle))++self.invalid;}
    static void* BE_CALL unbox(void*,void* object) {return static_cast<AbiFixtureObject*>(object)->data;}
    static void* BE_CALL invoke(void* context,const void* method,void* object,void** args,void** error) {
        auto& self=*static_cast<Runtime*>(context);*error=nullptr;if(object!=&self.manager) {++self.invalid;*error=&self.manager;return nullptr;}
        if(method==&self.hide) {
            ++self.hideCalls;self.entityBits=*static_cast<int*>(args[0]);self.npcBits=*static_cast<int*>(args[2]);
            if(args[1]||args[3])++self.invalid;self.owned=++self.counter;
            if(self.hideThrows) {*error=&self.manager;return nullptr;}
            const auto returned=self.noHandle?0:self.owned;std::memcpy(self.result.data,&returned,4);return &self.result;
        }
        if(method==&self.revert) {
            ++self.revertCalls;if(*static_cast<std::uint32_t*>(args[0])!=self.owned)++self.invalid;
            if(!self.deferred)self.owned=0;if(self.revertThrows)*error=&self.manager;return nullptr;
        }
        ++self.invalid;*error=&self.manager;return nullptr;
    }
    BE_HostApiV1 api() {BE_HostApiV1 host{};host.context=this;host.resolve_class=klass;host.resolve_method=method;
        host.runtime_invoke=invoke;host.object_unbox=unbox;host.gchandle_new=root;host.gchandle_free=free;return host;}
    NativeEntityWithdrawal::StorageObservation observe(void*,std::uint32_t handle) {return {storage,counter,handle&&owned==handle};}
};
struct Fixture {
    Runtime vm;BE_HostApiV1 runtime=vm.api();NativeObjectPins pins;NativeEntityWithdrawal lease;Adapter adapter;
    NativeEntityWithdrawal::Observe observer=[this](void* manager,std::uint32_t handle){return vm.observe(manager,handle);};
    Fixture() {pins.bind(&vm,[this](std::uintptr_t object){return Runtime::root(&vm,reinterpret_cast<void*>(object),0);},
        [this](unsigned handle){Runtime::free(&vm,handle);});lease.bind(&runtime,"1.5.3",pins);}
    bool acquire() {return lease.acquire(adapter,&vm.manager,observer);}
    bool retire() {return lease.retire(&vm.manager,observer);}
};
int run(const wchar_t* path) {
    auto dll=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    check(dll&&dll==GetModuleHandleW(L"GameAssembly.dll"),"withdrawal test loads only its explicit synthetic metadata DLL");
    {
        Fixture f;f.adapter.verified=false;
        check(!f.acquire()&&!f.vm.hideCalls&&!f.lease.retained(),"uncertified adapter cannot mutate personal native entity storage");
    }
    {
        Fixture f;bool nested=true;f.observer=[&](void* manager,std::uint32_t handle) {
            if(!handle)nested=f.acquire();return f.vm.observe(manager,handle);
        };
        check(f.acquire()&&!nested&&f.vm.hideCalls==1,"native storage observation cannot re-enter and acquire the same lease twice");
        f.retire();
    }
    {
        Fixture f;f.observer=[&](void* manager,std::uint32_t handle) {f.adapter.verified=false;return f.vm.observe(manager,handle);};
        check(!f.acquire()&&!f.vm.hideCalls&&!f.lease.retained()&&f.pins.sweep()&&f.pins.safeToUnload(),
            "native observation revoking certification prevents the first personal-world mutation");
    }
    {
        Fixture f;check(f.acquire()&&f.vm.hideCalls==1&&f.lease.retained()&&!f.pins.safeToUnload(),"native acquisition retains its private manager and observes its actual owned handle");
        check(f.vm.entityBits==(4|16|64)&&f.vm.npcBits==(2|8|32|128)&&!f.vm.invalid,"native withdrawal masks come from the current runtime enum fields");
        check(!f.acquire()&&f.vm.hideCalls==1,"repeated admission cannot acquire a second hidden personal-world lease");
        check(f.retire()&&f.vm.revertCalls==1&&!f.lease.retained()&&f.pins.sweep()&&f.pins.safeToUnload()&&f.vm.roots.empty(),"only the privately acquired handle is reverted and roots release on the game thread");
        check(f.retire()&&f.vm.revertCalls==1,"native lease retirement is idempotent");
        const auto info=f.lease.status();check(info["personal_world_restored"]==false&&info["official_reporting_isolation"]==false&&info["isolation_verified"]==false,
            "native entity lease retirement cannot certify full personal restoration or account reporting isolation");
    }
    {
        Fixture f;bool stable=true;for(unsigned cycle=0;cycle<20;++cycle) {
            stable=stable&&f.acquire()&&f.retire()&&f.pins.sweep()&&f.pins.safeToUnload()&&f.vm.roots.empty();
        }
        check(stable&&f.vm.hideCalls==20&&f.vm.revertCalls==20,"repeated entries acquire fresh private leases without retaining old roots or handles");
    }
    {
        Fixture f;f.acquire();bool nested=true;f.observer=[&](void* manager,std::uint32_t handle) {
            nested=f.retire();return f.vm.observe(manager,handle);
        };
        check(f.retire()&&!nested&&f.vm.revertCalls==1,"native retirement observation cannot re-enter the same lease restoration");
    }
    {
        Fixture f;f.acquire();f.vm.deferred=true;
        check(!f.retire()&&f.lease.retained()&&f.vm.revertCalls==1,"native revert returning does not prove the handle retired");
        check(!f.retire()&&f.vm.revertCalls==1,"pending native restoration is observed without redispatch");
        f.vm.owned=0;check(f.retire()&&f.vm.revertCalls==1&&f.pins.sweep()&&f.pins.safeToUnload(),"later native storage observation retires the exact lease without another revert");
    }
    for(unsigned mode=0;mode<3;++mode) {
        Fixture f;f.acquire();if(mode==0)++f.vm.storage;if(mode==1)--f.vm.counter;
        AbiFixtureObject other{&f.vm.managerClass,{}};
        check(!(mode==2?f.lease.retire(&other,f.observer):f.retire())&&f.lease.retained()&&!f.vm.revertCalls,
            "different manager, native storage or regressed handle allocator cannot restore through a stale scene");
    }
    {
        Fixture f;f.vm.hideThrows=true;check(!f.acquire()&&f.lease.retained()&&!f.pins.safeToUnload()&&!f.acquire()&&f.vm.hideCalls==1,
            "ambiguous native acquisition keeps recovery evidence and cannot be blindly repeated");
    }
    {
        Fixture f;f.vm.noHandle=true;check(!f.acquire()&&f.lease.retained()&&!f.retire()&&!f.vm.revertCalls,
            "missing native handle cannot be guessed from the current allocator counter");
    }
    {
        Fixture f;f.acquire();f.vm.revertThrows=true;
        check(!f.retire()&&f.lease.retained()&&f.retire()&&f.vm.revertCalls==1,
            "ambiguous revert is confirmed from subsequent native storage observation without calling it twice");
    }
    {
        Fixture f;f.acquire();bool refused=false;try {f.lease.bind(&f.runtime,"unknown",f.pins);}catch(...) {refused=true;}
        check(refused&&f.lease.retained()&&!f.retire()&&!f.vm.revertCalls,"unknown runtime version retains recovery evidence and disables old native entrypoints");
    }
    FreeLibrary(dll);return 0;
}
int wmain(int argc,wchar_t** argv) {try {if(argc!=2)throw std::runtime_error("explicit fixture DLL required");return run(argv[1]);}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
