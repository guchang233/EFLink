#include "authority/native_object_bindings.h"
#include <iostream>
using namespace eflink::worldauthority;
static void check(bool value,const char* name) {if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
template<class F> static bool rejects(F f) {try {f();return false;}catch(...) {return true;}}
struct Runtime {
    std::map<std::uint64_t,std::uintptr_t> handles;
    std::uint64_t next=0,allocated=0,released=0,releaseCalls=0;bool zero=false,throwRetain=false,throwRelease=false;
    void bind(NativeObjectPins& pins) {
        pins.bind(this,[this](std::uintptr_t address) {
            if(throwRetain)throw std::runtime_error("ambiguous native allocation");
            if(zero)return std::uint64_t(0);const auto handle=++next;handles[handle]=address;++allocated;return handle;
        },[this](std::uint64_t handle) {
            ++releaseCalls;if(throwRelease)throw std::runtime_error("ambiguous native release");
            if(handles.erase(handle)!=1)throw std::runtime_error("double or foreign release");++released;
        });
    }
};
static void census(NativeObjectBindings& bindings,const char* kind,const char* key,NativeAllocation allocation) {
    bindings.reconcile(kind,{{key,allocation,bindings.retain(kind,key,allocation)}});
}
int main() {
    Runtime vm;NativeObjectPins pins;vm.bind(pins);
    auto root=pins.retain(0x1000);check(pins.live()==1&&pins.matches(root,0x1000),"real root provider backs a retained native address");
    auto borrowed=root;root.reset();pins.sweep();check(vm.released==0,"borrowing a root keeps its native object retained");
    std::thread worker([&]{borrowed.reset();});worker.join();
    check(vm.released==0&&pins.live()==1,"last worker-thread release queues rather than calling game APIs");
    check(pins.sweep()&&pins.live()==0&&vm.released==1,"owning game thread releases the queued native root exactly once");
    check(pins.sweep()&&vm.released==1,"repeated root cleanup is idempotent");
    bool foreignRejected=false;std::thread wrong([&]{foreignRejected=rejects([&]{vm.bind(pins);})&&rejects([&]{pins.retain(0x1000);})&&!pins.sweep();});wrong.join();
    check(foreignRejected,"root allocation and provider replacement cannot migrate onto a worker");
    NativeObjectBindings bindings{NativeDomain::HostWorld,&pins};NativeAllocation ore{0x1000,1,2,3,4,5};
    census(bindings,"mineral","ore",ore);const auto cookie=bindings.cookie("mineral","ore"),allocations=vm.allocated;
    census(bindings,"mineral","ore",ore);pins.sweep();
    check(vm.allocated==allocations&&pins.live()==1,"unchanged native census reuses the existing managed root");
    auto replacement=ore;++replacement.allocation;
    {
        auto prepared=bindings.preview();census(prepared,"mineral","ore",replacement);
        check(!bindings.commitWith(prepared,[]{return false;})&&bindings.cookie("mineral","ore")==cookie&&pins.live()==2,
            "rejected authority frame preserves its old binding and root");
    }
    pins.sweep();check(pins.live()==1&&bindings.resolve(NativeDomain::HostWorld,"mineral","ore",ore),
        "discarding a candidate releases only its newly retained native allocation");
    check(rejects([&]{bindings.reconcile("mineral",{{"ore",replacement,{}}});})&&pins.live()==1,
        "production bindings reject a source allocation without managed retention");
    bool used=false,probed=false;
    auto probe=[&](NativeAllocation value){probed=true;return value;};auto use=[&](const NativeBinding&){used=true;return true;};
    check(!bindings.withResolved(NativeDomain::GuestRoom,"mineral","ore",cookie,probe,use)&&!probed&&!used,
        "wrong world domain cannot probe or use a personal native object");
    check(!bindings.withResolved(NativeDomain::HostWorld,"mineral","ore",cookie+1,probe,use)&&!probed&&!used,
        "a stale or fabricated private cookie never exposes a native pointer");
    check(!bindings.withResolved(NativeDomain::HostWorld,"mineral","ore",cookie,[&](NativeAllocation){return replacement;},use)&&!used,
        "GC retention alone cannot prove a pooled native object is still the same allocation");
    check(bindings.withResolved(NativeDomain::HostWorld,"mineral","ore",cookie,probe,use)&&used,
        "compiled adapter can synchronously use a rooted and revalidated native binding");
    used=false;check(!bindings.withResolved(NativeDomain::HostWorld,"mineral","ore",cookie,[&](NativeAllocation value){bindings.clear();return value;},use)&&!used&&pins.live()==1,
        "reentrant retirement during native validation cannot dispatch an old operation");
    pins.sweep();check(pins.live()==0,"operation access holds its root until native validation returns");
    census(bindings,"mineral","ore",replacement);const auto newCookie=bindings.cookie("mineral","ore");
    check(newCookie>cookie&&bindings.withResolved(NativeDomain::HostWorld,"mineral","ore",newCookie,probe,[&](const NativeBinding&){
        bindings.clear();pins.sweep();return pins.live()==1;
    }),"native callback can retire a binding without collecting its in-flight receiver");
    check(pins.sweep()&&pins.safeToUnload()&&vm.handles.empty(),"receiver release after callback completion permits native teardown");
    vm.zero=true;check(rejects([&]{census(bindings,"mineral","ore",ore);})&&bindings.size()==0&&pins.live()==0,
        "failed native GC root allocation cannot enter a binding frame");vm.zero=false;pins.sweep();
    Runtime otherVm;NativeObjectPins other;otherVm.bind(other);auto alien=other.retain(ore.address);
    check(rejects([&]{bindings.reconcile("mineral",{{"ore",ore,alien}});}),"root from another runtime cannot satisfy a binding retention contract");
    alien.reset();other.sweep();
    {
        Runtime failing;NativeObjectPins uncertain;failing.bind(uncertain);auto held=uncertain.retain(0x2000);held.reset();failing.throwRelease=true;
        check(!uncertain.sweep()&&uncertain.releaseUncertain()&&!uncertain.safeToUnload()&&uncertain.live()==1,
            "ambiguous root release retains failure evidence and closes the teardown barrier");
        const auto attempts=failing.releaseCalls;failing.throwRelease=false;
        check(!uncertain.sweep()&&failing.releaseCalls==attempts&&rejects([&]{uncertain.retain(0x3000);}),
            "uncertain native handle is never retried after its numeric slot might have been reused");
    }
    {
        Runtime failing;NativeObjectPins uncertain;failing.bind(uncertain);failing.throwRetain=true;
        check(rejects([&]{uncertain.retain(0x2000);})&&uncertain.releaseUncertain()&&!uncertain.sweep()&&!uncertain.safeToUnload(),
            "ambiguous allocation cannot be reported as clean just because no handle was returned");
    }
}
