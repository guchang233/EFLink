// Loads a tiny test-owned export fixture, never the installed game binary.
#include "unity_bridge.h"
#include "support/native_abi_fixture.h"
#include <iostream>
#include <set>
using eflink::unity::Api;
static void check(bool value,const char* name) {if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
template<class F> static bool rejects(F f) {try {f();return false;}catch(const std::runtime_error&) {return true;}}
struct Runtime {
    const void* allowed=nullptr;const void* baseClass=nullptr;void* result=nullptr;unsigned invoked=0,invalid=0,unboxed=0,next=0;
    std::set<unsigned> roots;
    static void* BE_CALL invoke(void* context,const void* method,void*,void**,void** error) {
        auto& self=*static_cast<Runtime*>(context);*error=nullptr;
        if(method!=self.allowed) {++self.invalid;*error=&self;return nullptr;}
        ++self.invoked;return self.result;
    }
    static unsigned BE_CALL root(void* context,void* object,int) {
        if(!object)return 0;auto& self=*static_cast<Runtime*>(context);const auto id=++self.next;self.roots.insert(id);return id;
    }
    static void BE_CALL release(void* context,unsigned handle) {static_cast<Runtime*>(context)->roots.erase(handle);}
    static void* BE_CALL unbox(void* context,void* object) {++static_cast<Runtime*>(context)->unboxed;return static_cast<AbiFixtureObject*>(object)->data;}
    static BE_Result BE_CALL resolveClass(void* context,const char*,const char*,const char*,BE_ResolvedClassV1* result) {
        *result={};result->class_info=static_cast<Runtime*>(context)->baseClass;return BE_Result_Ok;
    }
};
int run(const wchar_t* path) {
    auto fixture=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    check(fixture&&fixture==GetModuleHandleW(L"GameAssembly.dll"),"bridge test uses the explicit synthetic metadata DLL");
    AbiFixtureClass base{false,0,0,nullptr},derived{false,0,0,&base},other{false,0,0,nullptr},integer{true,4,4,nullptr};
    AbiFixtureMethod actual{&derived,nullptr,0x40,0},declaration{&base,&actual,0x440,0};
    AbiFixtureObject receiver{&derived,{}},boxed{&integer,{}};int number=42;std::memcpy(boxed.data,&number,sizeof number);
    Runtime vm;vm.allowed=&actual;vm.result=&boxed;vm.baseClass=&base;base.method=&declaration;
    BE_HostApiV1 host{};host.context=&vm;host.runtime_invoke=Runtime::invoke;
    host.object_unbox=Runtime::unbox;host.gchandle_new=Runtime::root;host.gchandle_free=Runtime::release;host.resolve_class=Runtime::resolveClass;
    Api api;api.bind(&host);const auto method=api.virtualGetter("FixtureBase","get_type","System.Int32","Fixture.dll","Fixture");
    check(method.method_info==&declaration&&!method.method_pointer&&
          rejects([&]{api.virtualGetter("FixtureBase","get_type","System.Int64","Fixture.dll","Fixture");}),
        "virtual declaration lookup validates metadata without requiring a base callable entry");
    check(api.virtualValue<int>(method,&receiver)==42&&vm.invoked==1&&vm.invalid==0&&vm.roots.empty(),
        "abstract base getter dispatches to the actual native implementation and releases roots");
    const auto calls=vm.invoked,unboxed=vm.unboxed;
    check(rejects([&]{api.virtualValue<std::uint64_t>(method,&receiver);})&&vm.invoked==calls+1&&vm.unboxed==unboxed&&vm.roots.empty(),
        "actual bridge rejects boxed width mismatch before unboxing without leaking managed roots");
    receiver.klass=&other;const auto before=vm.invoked;
    check(rejects([&]{api.virtualValue<int>(method,&receiver);})&&vm.invoked==before&&vm.roots.empty(),
        "unrelated instance cannot invoke a base virtual method");
    receiver.klass=&derived;declaration.implementation=nullptr;
    check(rejects([&]{api.virtualValue<int>(method,&receiver);})&&vm.invoked==before&&vm.invalid==0&&vm.roots.empty(),
        "missing implementation cannot fall back to an abstract declaration");
    declaration.implementation=&declaration;
    check(rejects([&]{api.virtualValue<int>(method,&receiver);})&&vm.invoked==before&&vm.invalid==0,
        "an abstract result from virtual lookup is rejected before runtime invocation");
    declaration.implementation=&actual;actual.count=1;
    check(rejects([&]{api.virtualValue<int>(method,&receiver);})&&vm.invoked==before,
        "virtual target parameter mismatch cannot cross the bridge");
    actual.count=0;declaration.flags|=0x10;
    check(rejects([&]{api.virtualValue<int>(method,&receiver);})&&vm.invoked==before&&vm.roots.empty(),
        "static method cannot enter instance virtual dispatch");
    declaration.flags=0x440;actual.flags|=0x10;
    check(rejects([&]{api.virtualValue<int>(method,&receiver);})&&vm.invoked==before,
        "static virtual lookup target is rejected");
    FreeLibrary(fixture);return 0;
}
int wmain(int count,wchar_t** args) {try {if(count!=2)throw std::runtime_error("explicit fixture DLL path required");return run(args[1]);}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
