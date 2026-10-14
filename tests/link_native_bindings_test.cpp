#include "authority/native_object_bindings.h"
#include <iostream>
#include <thread>
using namespace eflink::worldauthority;
static void check(bool value,const char* name) {if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
template<class F> static bool rejects(F f) {try {f();return false;}catch(...) {return true;}}
int main() {
    NativeObjectBindings bindings;
    NativeAllocation ore{0x1000,1,2,0,17,31},actor{0x2000,3,4,0,18,32};
    bindings.reconcile("mineral",{{"ore",ore}});
    const auto first=bindings.find("mineral","ore")->cookie;
    bindings.reconcile("player",{{"actor",actor}});
    check(bindings.size()==2&&bindings.find("player","actor")->cookie!=first,"all kinds share one private cookie allocator");
    bindings.reconcile("mineral",{{"ore",ore}});
    check(bindings.find("mineral","ore")->cookie==first,"unchanged allocation retains identity across complete censuses");
    auto pooled=ore;++pooled.allocation;
    check(!bindings.resolve("mineral","ore",pooled),"same address with recycled allocation cannot resolve old object");
    bindings.reconcile("mineral",{{"ore",pooled}});
    const auto replacement=bindings.find("mineral","ore")->cookie;
    check(replacement>first&&bindings.resolve("mineral","ore",pooled),"pooled replacement receives a new lifecycle cookie");
    auto changed=pooled;++changed.secondaryId;
    check(!bindings.resolve("mineral","ore",changed),"native secondary identity changes invalidate the binding");
    check(rejects([&]{bindings.reconcile("mineral",{{"ore",pooled},{"ore",changed}});}),"duplicate stable keys reject the entire census");
    check(bindings.find("mineral","ore")->cookie==replacement&&bindings.size()==2,"failed census preserves every previous binding");
    check(rejects([&]{bindings.reconcile("mineral",{{"ore",pooled},{"alias",pooled}});}),"two stable keys cannot alias one native allocation in a kind");
    check(rejects([&]{bindings.reconcile("mineral",{{"bad",{0,9}}});}),"missing native allocation cannot enter a census");
    bindings.retireKind("mineral");
    check(!bindings.find("mineral","ore")&&bindings.find("player","actor"),"one kind retiring does not invalidate other kinds");
    bindings.reconcile("mineral",{{"ore",pooled}});
    check(bindings.find("mineral","ore")->cookie>replacement,"disappearance and reappearance cannot reuse a lifecycle");
    const auto beforeReset=bindings.find("mineral","ore")->cookie;
    bindings.clear();bindings.reconcile("mineral",{{"ore",pooled}});
    check(bindings.find("mineral","ore")->cookie>beforeReset,"scene teardown cannot resurrect an old cookie");
    bool wrongThreadRejected=false;
    std::thread other([&]{wrongThreadRejected=rejects([&]{bindings.retireKind("mineral");});});other.join();
    check(wrongThreadRejected&&bindings.size()==1,"non-game-thread reconciliation cannot mutate retained bindings");
    for(const auto& kind:{"enemy","mechanism","destructible","skill","projectile","summon","effect","wave","drop_display"}) {
        bindings.reconcile(kind,{{"object",actor}});
        check(bindings.resolve(kind,"object",actor)!=nullptr,"shared identity rules support every declared world kind");
    }
    check(rejects([&]{bindings.reconcile("reward",{{"item",actor}});}),"account rewards are not shared-world binding kinds");
    const auto retained=bindings.find("mineral","ore")->cookie;
    auto trial=bindings.preview();
    trial.reconcile("mineral",{});trial.reconcile("player",{{"new-player",ore}});
    check(!bindings.commitWith(trial,[]{return false;})&&bindings.find("mineral","ore")->cookie==retained&&
        !bindings.find("player","new-player"),"authority rejection cannot retire or expose any staged native identity");
    check(rejects([&]{bindings.commitWith(trial,[]{throw std::runtime_error("invalid frame");return true;});})&&
        bindings.find("mineral","ore"),"authority validation exception preserves the native allocation table");
    check(bindings.commitWith(trial,[]{return true;})&&!bindings.find("mineral","ore")&&
        bindings.find("player","new-player"),"accepted cross-kind frame commits retirements and new bindings together");
    check(rejects([&]{bindings.commitWith(trial,[]{return true;});}),"native transaction cannot be committed twice");
    auto stale=bindings.preview();bindings.retireKind("player");bool called=false;
    check(rejects([&]{bindings.commitWith(stale,[&]{called=true;return true;});})&&!called,
        "stale transaction cannot publish authority before discovering changed bindings");
    NativeObjectBindings foreign;auto another=foreign.preview();
    check(rejects([&]{bindings.commitWith(another,[]{return true;});}),"foreign native transaction is rejected");
    auto reentrant=bindings.preview();
    check(rejects([&]{bindings.commitWith(reentrant,[&]{bindings.clear();return true;});})&&bindings.size()!=0,
        "authority callback cannot mutate bindings while publishing a frame");
    check(rejects([&]{bindings.commitWith(reentrant,[&]{reentrant.clear();return true;});})&&bindings.size()!=0,
        "authority callback cannot change the prepared frame's identities during publication");
}
