#include "authority/native_construction_state.h"
#include <iostream>
using namespace eflink::worldauthority;
static void check(bool value,const char* name) {if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
template<class F> static bool rejects(F f) {try {f();return false;}catch(...) {return true;}}
int main() {
    Observation ore{"ore","mineral",{{"template","doodad_gameplay_config"},{"hp_ratio",10000},{"max_hp_ratio",10000}},1};
    const NativeConstructionReading native{"interactive_entity_template",{0,0,0,1},{1,2,3}};
    applyNativeConstruction(ore,native);
    check(ore.state["template"]=="doodad_gameplay_config"&&ore.state["archetype"]=="interactive_entity_template"&&ore.state["scale"]==native.scale,
        "native entity archetype remains distinct from gameplay resource configuration");
    Host host;const Scope scope{7,std::string(32,'b'),1};host.reset(scope,"fixture-game");host.observe("mineral",{ore});
    Replica guest;check(guest.baseline(host.baseline(),scope,true)&&guest.snapshot()==host.baseline()["objects"],
        "late join retains actual construction configuration and transform in the unified baseline");
    auto before=ore.state;
    for(auto bad:{NativeConstructionReading{"",{0,0,0,1},{1,1,1}},NativeConstructionReading{"known",{0,0,0,0},{1,1,1}},
                  NativeConstructionReading{"known",{0,0,0,1},{NAN,1,1}}})
        check(rejects([&]{applyNativeConstruction(ore,bad);})&&ore.state==before,
            "incomplete construction evidence cannot partially replace a native observation");
    const auto revision=guest.version();const auto original=host.find("mineral","ore")->ref;
    auto changed=native;changed.scale={2,2,2};applyNativeConstruction(ore,changed);host.observe("mineral",{ore});
    check(host.find("mineral","ore")->ref==original&&guest.delta(*host.changesSince(revision),true)&&guest.snapshot()==host.baseline()["objects"],
        "native transform configuration changes use the same lifecycle, version and delta");
    for(const auto* kind:{"player","enemy","mechanism","destructible","skill","projectile","summon","effect","wave","drop_display"}) {
        auto fact=Observation{"object",kind,{{"active",true}},1};applyNativeConstruction(fact,native);
        check(validStateForKind(kind,fact.state),"the construction schema has no mineral-specific identity or serialization");
    }
    auto forged=ore.state;forged["native_id"]=999;
    check(!validStateForKind("mineral",forged),"construction metadata never accepts a transmitted engine or account instance ID");
    return 0;
}
