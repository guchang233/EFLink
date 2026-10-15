// Executes production NativeMineEntry against an explicitly loaded, test-owned
// metadata fixture. No installed game library, account or official connection.
#include "authority/native_mine_entry.h"
#include "support/native_abi_fixture.h"
#include <iostream>
#include <memory>
#include <set>
using namespace eflink::worldauthority;
static void check(bool value,const char* name) {if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
template<class F> static bool rejects(F f) {try {f();return false;}catch(...) {return true;}}
struct Runtime {
    AbiFixtureClass entity{false,0,0,nullptr},actorClass{false,0,0,&entity},nature{false,0,0,nullptr},oreClass{false,0,0,&nature},
        managerClass{false,0,0,nullptr},pointerClass{true,16,8,nullptr},integer{true,4,4,nullptr},wide{true,8,8,nullptr},boolean{true,1,1,nullptr};
    AbiFixtureType entityType{"Beyond.Gameplay.Core.Entity",&entity},pointerType{"Beyond.ObjectPtr<Beyond.Gameplay.Core.Entity>",&pointerClass},
        enumType{"Beyond.Gameplay.ENatureOperation",&integer},wideType{"System.UInt64",&wide};
    AbiFixtureMethod ctor{&pointerClass,nullptr,0,1,".ctor",{"System.Void"},{&entityType}},
        operation{&managerClass,nullptr,0,3,"DoOperation",{"System.Boolean",&boolean},{&pointerType,&enumType,&wideType}},
        getter{&oreClass,nullptr,0x40,0,"get_natureResourceId",{"System.UInt64",&wide}},
        declaration{&nature,&getter,0x440,0,"get_natureResourceId",{"System.UInt64",&wide}};
    AbiFixtureField mining{"Mining",enumType,7}; // Deliberately not a guessed game enum number.
    AbiFixtureObject actor{&actorClass,{}},ore{&oreClass,{}},manager{&managerClass,{}},idBox{&wide,{}},boolBox{&boolean,{}};
    std::uint64_t resource=12345,seenId=0;void* seenActor=nullptr;int seenMining=0;
    unsigned constructors=0,operations=0,nextRoot=0,releases=0,invalid=0;bool accept=true,ctorError=false;
    std::set<unsigned> roots;std::vector<std::unique_ptr<AbiFixtureObject>> boxes;std::function<void()> onConstruct;
    Runtime() {pointerClass.method=&ctor;nature.method=&declaration;integer.field=&mining;}
    static BE_Result BE_CALL klass(void* context,const char* assembly,const char* space,const char* name,BE_ResolvedClassV1* result) {
        auto& self=*static_cast<Runtime*>(context);*result={};
        if(std::strcmp(assembly,"Gameplay.Beyond.dll")||std::strcmp(space,"Beyond.Gameplay"))return BE_Result_NotFound;
        if(!std::strcmp(name,"ENatureOperation"))result->class_info=&self.integer;
        else if(!std::strcmp(name,"INatureResource"))result->class_info=&self.nature;
        return result->class_info?BE_Result_Ok:BE_Result_NotFound;
    }
    static BE_Result BE_CALL method(void* context,const BE_MethodDescriptorV1* d,BE_ResolvedMethodV1* result) {
        auto& self=*static_cast<Runtime*>(context);*result={};
        if(!std::strcmp(d->assembly_name,"Gameplay.Beyond.dll")&&!std::strcmp(d->namespace_name,"Beyond.Gameplay")&&
           !std::strcmp(d->class_name,"NatureResourceManager")&&!std::strcmp(d->method_name,"DoOperation")&&
           d->parameter_count==3&&!std::strcmp(d->parameter_types,"Beyond.ObjectPtr<Beyond.Gameplay.Core.Entity>|Beyond.Gameplay.ENatureOperation|System.UInt64")&&
           !std::strcmp(d->return_type,"System.Boolean")) {result->method_info=&self.operation;return BE_Result_Ok;}
        ++self.invalid;return BE_Result_NotFound;
    }
    static void* BE_CALL allocate(void* context,const void* klass) {
        auto& self=*static_cast<Runtime*>(context);if(klass!=&self.pointerClass)return nullptr;
        self.boxes.push_back(std::make_unique<AbiFixtureObject>(AbiFixtureObject{&self.pointerClass,{}}));return self.boxes.back().get();
    }
    static void* BE_CALL unbox(void*,void* object) {return object?static_cast<AbiFixtureObject*>(object)->data:nullptr;}
    static unsigned BE_CALL root(void* context,void* object,int) {
        if(!object)return 0;auto& self=*static_cast<Runtime*>(context);auto id=++self.nextRoot;self.roots.insert(id);return id;
    }
    static void BE_CALL free(void* context,unsigned id) {auto& self=*static_cast<Runtime*>(context);if(!self.roots.erase(id))++self.invalid;++self.releases;}
    static void* BE_CALL invoke(void* context,const void* method,void* object,void** args,void** error) {
        auto& self=*static_cast<Runtime*>(context);*error=nullptr;
        if(method==&self.ctor) {
            ++self.constructors;if(self.ctorError) {*error=&self.actor;return nullptr;}
            std::memcpy(object,&args[0],sizeof(void*));if(self.onConstruct)self.onConstruct();return nullptr;
        }
        if(method==&self.getter&&object==&self.ore) {std::memcpy(self.idBox.data,&self.resource,8);return &self.idBox;}
        if(method==&self.operation&&object==&self.manager) {
            ++self.operations;std::memcpy(&self.seenActor,args[0],sizeof(void*));self.seenMining=*static_cast<int*>(args[1]);self.seenId=*static_cast<std::uint64_t*>(args[2]);
            std::memcpy(self.boolBox.data,&self.accept,1);return &self.boolBox;
        }
        ++self.invalid;*error=&self.actor;return nullptr;
    }
    BE_HostApiV1 api() {
        BE_HostApiV1 host{};host.context=this;host.resolve_class=klass;host.resolve_method=method;host.runtime_invoke=invoke;
        host.object_new=allocate;host.object_unbox=unbox;host.gchandle_new=root;host.gchandle_free=free;return host;
    }
};
struct FixtureAdapter:NativeAdapter {
    Runtime vm;BE_HostApiV1 runtime=vm.api();NativeMineEntry entry;Host host;
    NativeBinding actor{"player","hero",{reinterpret_cast<std::uintptr_t>(&vm.actor),11,0,1,50,0},10,NativeDomain::HostWorld};
    NativeBinding ore{"mineral","ore",{reinterpret_cast<std::uintptr_t>(&vm.ore),21,31,1,60,12345},20,NativeDomain::HostWorld};
    bool certified=true,repeat=false,repeated=false;unsigned cancelled=0,validations=0;std::function<void()> onValidate;
    std::string version=NativeMineEntry::game;
    FixtureAdapter() {
        entry.bind(&runtime,version);host.reset({17,"0123456789abcdef0123456789abcdef",1},version);
        host.observeFrame({{"player",{{"hero","player",{{"active",true}},10}}},
            {"mineral",{{"ore","mineral",{{"hp_ratio",10000},{"max_hp_ratio",10000},{"collected",false}},20}}}});
        if(!host.bind(2,99,host.find("player","hero")->ref)||!host.resume(*this))throw std::runtime_error("fixture admission");
    }
    ~FixtureAdapter() {host.reset();}
    std::string gameVersion() const override {return version;}
    std::string path() const override {return NativeMineEntry::path;}
    bool executionVerified() const override {return certified;}
    bool isolationVerified() const override {return certified;}
    bool recoveryVerified() const override {return certified;}
    bool dualClientVerified() const override {return certified;}
    NativeActorContext actorContext(const Object&) const override {return {2,NativeActorDomain::RoomProxy};}
    bool supports(const std::string& kind,const std::string& op) const override {return kind=="mineral"&&op=="mine";}
    bool validate(const Intent&,const Object&,const Object&) const override {
        auto& self=*const_cast<FixtureAdapter*>(this);++self.validations;if(self.onValidate)self.onValidate();return true;
    }
    bool begin(const Ticket& ticket,const NativeDispatchPermit& permit) override {
        const bool result=entry.invoke(*this,ticket,permit,actor,ore,&vm.manager);
        if(repeat)repeated=entry.invoke(*this,ticket,permit,actor,ore,&vm.manager);return result;
    }
    void cancel(const Ticket&) noexcept override {++cancelled;}
    Intent intent(unsigned request=1,unsigned action=1) {
        return {host.scope(),2,99,request,action,1,host.find("mineral","ore")->version,host.find("player","hero")->ref,host.find("mineral","ore")->ref,"mine"};
    }
    bool submit(unsigned request=1,unsigned action=1) {return host.submit(intent(request,action).json(),2,*this,100).has_value();}
};
int run(const wchar_t* path) {
    auto fixture=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    check(fixture&&fixture==GetModuleHandleW(L"GameAssembly.dll"),"mining test uses only its explicit synthetic runtime DLL");
    {
        FixtureAdapter a;const auto before=a.host.baseline();a.repeat=true;
        check(a.submit()&&a.vm.operations==1&&a.vm.constructors==1&&!a.repeated&&a.entry.calls()==1&&a.entry.acceptedCalls()==1,
            "Host-issued permission executes production native entry exactly once");
        check(a.vm.seenActor==&a.vm.actor&&a.vm.seenMining==7&&a.vm.seenId==12345&&a.vm.invalid==0&&a.vm.roots.empty(),
            "closed native constructor, runtime enum and actual resource identity reach native DoOperation without leaked roots");
        a.host.poll(200);
        check(a.host.baseline()==before&&a.host.status()["results_confirmed"]==0,
            "accepted native dispatch cannot change ore facts or manufacture a result witness");
        check(!a.submit()&&a.vm.operations==1,"replayed intent cannot dispatch the native operation twice");
        a.host.poll(1600);check(a.host.status()["pending"]==0&&a.cancelled==1&&a.host.baseline()==before,
            "missing observed result expires without predicting mining success");
    }
    {
        FixtureAdapter a;a.vm.accept=false;check(!a.submit()&&a.vm.operations==1&&a.entry.acceptedCalls()==0&&a.vm.roots.empty(),"native refusal consumes request and releases every temporary root");
        check(!a.submit()&&a.vm.operations==1,"ambiguous or refused native action is not automatically retried");
    }
    for(unsigned mode=0;mode<4;++mode) {
        FixtureAdapter a;if(mode==0)a.actor.domain=NativeDomain::Personal;if(mode==1)a.ore.domain=NativeDomain::GuestRoom;
        if(mode==2)++a.actor.cookie;if(mode==3)++a.ore.cookie;
        check(!a.submit()&&!a.vm.operations&&!a.vm.constructors&&a.vm.roots.empty(),"wrong native domain or stale private cookie cannot enter native mining");
    }
    {
        FixtureAdapter a;++a.vm.resource;check(!a.submit()&&!a.vm.constructors&&!a.vm.operations&&a.vm.roots.empty(),"native resource-ID reuse is rejected before argument construction");
    }
    {
        FixtureAdapter a;a.vm.entityType.name="Wrong.Entity";
        check(!a.submit()&&!a.vm.constructors&&!a.vm.operations&&a.vm.roots.empty(),"different closed reference constructor cannot receive a native actor");
    }
    {
        FixtureAdapter a;a.vm.ctorError=true;check(!a.submit()&&a.vm.constructors==1&&!a.vm.operations&&a.vm.roots.empty(),"managed constructor failure does not call mining or retain temporary roots");
    }
    {
        FixtureAdapter a;a.vm.onConstruct=[&]{a.certified=false;};
        check(!a.submit()&&!a.vm.operations&&a.vm.roots.empty(),"certificate revocation during native construction is rechecked before dispatch");
    }
    {
        FixtureAdapter a;a.vm.onConstruct=[&]{a.host.revoke(2);};
        check(!a.submit()&&!a.vm.operations&&a.vm.roots.empty(),"membership revocation during argument preparation cancels native dispatch");
    }
    {
        FixtureAdapter a;a.onValidate=[&]{if(a.validations>=3)a.certified=false;};
        check(!a.submit()&&!a.vm.operations&&a.vm.roots.empty(),"reentrant validation cannot reopen a revoked execution certificate");
    }
    {
        FixtureAdapter a;a.onValidate=[&]{a.host.reset();};
        check(!a.submit()&&!a.vm.operations,"scene reset during initial validation retains no dangling object or member reference");
    }
    {
        FixtureAdapter a;check(rejects([&]{a.entry.bind(&a.runtime,"unknown");})&&!a.entry.contractsResolved()&&!a.submit()&&!a.vm.operations,
            "unknown game version closes a previously resolved native mining entry");
    }
    FreeLibrary(fixture);return 0;
}
int wmain(int count,wchar_t** args) {try {if(count!=2)throw std::runtime_error("explicit test-owned fixture DLL path required");return run(args[1]);}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
