// Canonical native observation rules only; no game objects are instantiated.
#include "authority/world_authority.h"
#include <iostream>
using namespace eflink::worldauthority;
static void check(bool value,const char* name) {if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
template<class F> static bool rejects(F f) {try {f();return false;}catch(...) {return true;}}
int main() {
    Host host;const Scope scope{7,std::string(32,'a'),1};host.reset(scope,"fixture-game");
    std::vector<Observation> party;
    for(unsigned slot=0;slot<4;++slot)party.push_back({"party-"+std::to_string(slot),"player",
        {{"template","chr_fixture"},{"hp",100},{"max_hp",100},{"rotation",{0,0,0,1}},
         {"party_slot",slot},{"controlled",slot==0},{"weapon_template","wpn_fixture"},{"weapon_state",1}},slot+1});
    host.observe("player",party);Replica guest;
    check(guest.baseline(host.baseline(),scope,true)&&guest.snapshot().size()==4,
        "all four native party slots use canonical object references and a single baseline");
    const auto original=host.find("player","party-0")->ref;
    const auto revision=guest.version();party[0].state["weapon_template"]="wpn_changed";party[0].state["weapon_state"]=2;
    host.observe("player",party);
    check(host.find("player","party-0")->ref==original&&host.find("player","party-0")->version==2&&
          guest.delta(*host.changesSince(revision),true)&&guest.snapshot()==host.baseline()["objects"],
        "native equipment and hold changes advance the same character version and delta");
    party[0].state["weapon_template"]="";party[0].state["weapon_state"]=0;
    party[0].state["controlled"]=false;party[1].state["controlled"]=true;
    host.observe("player",party);Replica late;
    check(late.baseline(host.baseline(),scope,true)&&late.snapshot()==host.baseline()["objects"]&&
          host.find("player","party-0")->state["weapon_template"]=="",
        "late join receives observed unequip and character selection from the complete object baseline");
    const auto before=host.baseline();auto invalid=party;
    invalid[0].state["weapon_instance_id"]=123;
    check(rejects([&]{host.observe("player",invalid);})&&host.baseline()==before,
        "account equipment instance identifiers cannot enter canonical public state");
    invalid=party;invalid[0].state["weapon_template"]="../../account/item";
    check(rejects([&]{host.observe("player",invalid);})&&host.baseline()==before,
        "weapon resources cannot become filesystem or arbitrary resource paths");
    for(auto state:{Json{{"party_slot",4}},Json{{"party_slot",-1}},Json{{"weapon_state",6}},Json{{"controlled",1}},Json{{"weapon_template",123}}})
        check(!validStateForKind("player",state),"unsupported native appearance values are rejected without coercion");
    check(!validStateForKind("mineral",{{"party_slot",0}})&&!validStateForKind("effect",{{"weapon_state",1}}),
        "party presentation fields cannot masquerade as another shared object kind");
    auto corrupt=host.baseline();corrupt["objects"][0]["kind"]="mineral";
    Replica refusing;check(!refusing.baseline(corrupt,scope,true)&&refusing.needsBaseline(),
        "replicas apply the same kind restrictions as native host observations");
    ++party[0].cookie;host.observe("player",party);
    check(host.find("player","party-0")->ref!=original&&host.find(original)==nullptr&&host.find("player","party-0")->version==1,
        "replacing a native party allocation retires equipment and pose with its old lifecycle");
    const auto wire=host.baseline().dump();
    check(wire.find("cookie")==std::string::npos&&wire.find("instance_id")==std::string::npos,
        "appearance baselines carry public templates and room references without native account identity");
    return 0;
}
