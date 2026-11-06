// Contract checks for the read-only native event census and its production
// wiring. The client-side field/type declarations were cross-checked offline
// against global-metadata.dat, but no live EventManager or delegate call is
// simulated here: runtime layout and ABI remain uncertified. None of these
// checks claim isolation, recovery or full-sync verification.
#include "authority/native_event_registry.h"
#include "support/native_abi_fixture.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace eflink::worldauthority;
static void check(bool ok,const char* label) {
    if(!ok)throw std::runtime_error(label);
    std::cout<<"PASS "<<label<<std::endl;
}
static std::filesystem::path executableDir() {
    wchar_t buffer[MAX_PATH];
    auto length=GetModuleFileNameW(nullptr,buffer,MAX_PATH);
    if(!length||length>=MAX_PATH)return std::filesystem::current_path();
    return std::filesystem::path(buffer).parent_path();
}
static std::string readTreeFile(const char* relative) {
    namespace fs=std::filesystem;
    fs::path starts[]={fs::current_path(),executableDir()};
    for(const auto& start:starts) {
        auto dir=start;
        for(int i=0;i<8;++i) {
            auto path=dir/relative;
            if(fs::exists(path)) {std::ifstream in(path,std::ios::binary);std::ostringstream text;text<<in.rdbuf();return text.str();}
            if(!dir.has_parent_path()||dir.parent_path()==dir)break;
            dir=dir.parent_path();
        }
    }
    throw std::runtime_error(std::string("missing source file ")+relative);
}
static void guards() {
    NativeEventRegistry registry;
    NativeObjectPins pins;
    int manager=0,target=0;
    check(!registry.boundToRuntime(),"event census starts unbound");
    const auto status=registry.status();
    check(status.at("bound")==false&&status.at("scans")==0&&status.at("retained")==0&&status.at("clear")==0,"unbound census reports no scans or verdicts");
    check(status.at("read_only")==true&&status.at("isolation_verified")==false&&status.at("full_sync_verified")==false,"event census never certifies isolation or full sync");
    check(registry.observe(&manager,&target,pins)==NativeEventRegistry::Result::Unknown,"unbound census reports unknown rather than clear");
    check(!registry.observeClear(&target,pins),"unbound cleanup observation cannot release retained private data");
    bool threw=false;
    try {auto* value=registry.existingManager();(void)value;}catch(...) {threw=true;}
    check(threw,"existing manager lookup is refused while unbound");
    BE_HostApiV1 host{};
    threw=false;
    try {registry.bind(&host,"unknown");}catch(...) {threw=true;}
    check(threw&&!registry.boundToRuntime(),"unverified game version cannot bind the event census");
    threw=false;
    try {registry.bind(&host,"1.5.3");}catch(...) {threw=true;}
    check(threw&&!registry.boundToRuntime(),"missing native runtime cannot bind the event census");
    check(!registry.observeClear(&target,pins),"a failed bind leaves cleanup observation closed");
    check(registry.status().at("scans")==0,"guard rejections do not count as native scans");
}
static void wiring() {
    const auto world=readTreeFile("src/link_world.h");
    const auto registry=readTreeFile("src/authority/native_event_registry.h");
    const auto info=readTreeFile("src/authority/native_character_info.h");
    const auto members=readTreeFile("src/authority/native_members.h");
    check(world.find("authority/native_event_registry.h")!=std::string::npos,"production world includes the read-only event census");
    check(world.find("nativeEventRegistry.observeClear(info,nativePins)")!=std::string::npos,"private data cleanup uses the census as its independent observation");
    check(world.find("data->cleanup([this](void* info)")!=std::string::npos,"unload passes an observation callback instead of asserting cleanup");
    check(world.find("\"native_event_registry\",nativeEventRegistry.status()")!=std::string::npos,"diagnostics expose census state");
    check(world.find("boundToRuntime()")!=std::string::npos,"census binding is checked before use");
    check(info.find("!observed||!observed(info)")!=std::string::npos,"cleanup still requires an independent native observation");
    check(registry.find("read_only")!=std::string::npos&&registry.find("\"isolation_verified\",false")!=std::string::npos,"census stays read-only and uncertified");
    check(registry.find("Result::Clear")!=std::string::npos,"only an explicit clear verdict releases retained data");
    check(members.find("declared&&declared!=expected")!=std::string::npos,"generic T singleton return type is accepted instead of hard-rejected");
    check(members.find("il2cpp_class_is_assignable_from")!=std::string::npos,"the returned singleton instance is still type-checked");
}
// Mirrors only the metadata shape: Beyond.EventManager derives from
// Beyond.Singleton<Beyond.EventManager>, whose get_instanceOrNull declares T
// as its return type (il2cpp_class_from_type yields no class for T).
struct SingletonRuntime {
    AbiFixtureClass managerClass{false,0,0,nullptr},singletonClass{false,0,0,nullptr},foreignClass{false,0,0,nullptr};
    AbiFixtureMethod getter{&singletonClass,nullptr,0x10,0,"get_instanceOrNull",{"T",nullptr}};
    AbiFixtureMethod duplicate{&singletonClass,nullptr,0x10,0,"get_instanceOrNull",{"T",nullptr}};
    AbiFixtureObject manager{&managerClass,{}},foreign{&foreignClass,{}};
    void* result=nullptr;
    unsigned calls=0;
    SingletonRuntime() {managerClass.parent=&singletonClass;singletonClass.method=&getter;result=&manager;}
    static void* BE_CALL invoke(void* context,const void* method,void*,void**,void** error) {
        auto& self=*static_cast<SingletonRuntime*>(context);*error=nullptr;++self.calls;
        if(method!=&self.getter&&method!=&self.duplicate) {*error=&self.foreign;return nullptr;}
        return self.result;
    }
    BE_HostApiV1 api() {BE_HostApiV1 host{};host.context=this;host.runtime_invoke=invoke;return host;}
};
static void* lookup(SingletonRuntime& runtime) {
    auto host=runtime.api();eflink::unity::Api bridge;bridge.bind(&host);
    NativeMembers members(bridge);return members.existingSingleton(&runtime.managerClass);
}
template<class Mutate> static bool rejects(Mutate mutate) {
    SingletonRuntime runtime;mutate(runtime);
    try {(void)lookup(runtime);}catch(...) {return true;}
    return false;
}
static void singletonLookup(const wchar_t* path) {
    auto dll=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    check(dll&&dll==GetModuleHandleW(L"GameAssembly.dll"),"event registry test uses only its explicit synthetic runtime DLL");
    {
        SingletonRuntime runtime;
        check(lookup(runtime)==&runtime.manager&&runtime.calls==1,"generic T singleton getter resolves the existing manager without allocating");
    }
    {
        SingletonRuntime runtime;runtime.getter.returns=AbiFixtureType{"Beyond.EventManager",&runtime.managerClass};
        check(lookup(runtime)==&runtime.manager,"a concrete correct return type still resolves");
    }
    {
        SingletonRuntime runtime;runtime.result=nullptr;
        check(lookup(runtime)==nullptr,"an absent singleton instance is reported as absent, not as a clear verdict");
    }
    check(rejects([](SingletonRuntime& r){r.getter.returns=AbiFixtureType{"System.Object",&r.foreignClass};}),"a concrete wrong declared return type is rejected");
    check(rejects([](SingletonRuntime& r){r.getter.flags=0;}),"a non-static getter is rejected");
    check(rejects([](SingletonRuntime& r){r.getter.flags|=0x400;}),"an abstract getter is rejected");
    check(rejects([](SingletonRuntime& r){r.getter.count=1;}),"a getter with parameters is rejected");
    check(rejects([](SingletonRuntime& r){r.singletonClass.method=nullptr;}),"a missing non-allocating getter is rejected");
    check(rejects([](SingletonRuntime& r){r.managerClass.parent=nullptr;}),"a getter outside the base chain is rejected");
    check(rejects([](SingletonRuntime& r){r.getter.next=&r.duplicate;}),"ambiguous singleton getters are rejected");
    check(rejects([](SingletonRuntime& r){r.result=&r.foreign;}),"a returned instance of the wrong type is rejected");
    FreeLibrary(dll);
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=2)throw std::runtime_error("explicit fixture DLL required");
        guards();wiring();singletonLookup(argv[1]);
        std::cout<<"PASS native event registry"<<std::endl;return 0;
    }catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<std::endl;return 1;}
}
