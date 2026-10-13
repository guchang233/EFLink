#include "authority/world_authority.h"
#include <atomic>
#include <cstdio>
using namespace eflink::worldauthority;
int failures=0;
void check(bool ok,const char* name) {std::printf("%s %s\n",ok?"[ok]":"[FAIL]",name);if(!ok)++failures;}
template<class F> bool throws(F f) {try {f();return false;}catch(...) {return true;}}
Scope scene(std::uint64_t epoch=1) {return {17,"0123456789abcdef0123456789abcdef",epoch};}
Observation mineral(int hp=10000,std::uint64_t cookie=20,bool collected=false) {
    return {"ore-12","mineral",{{"template","quartz"},{"position",{1,2,3}},{"hp_ratio",hp},{"max_hp_ratio",10000},{"collected",collected}},cookie};
}
struct Fixture:NativeAdapter {
    bool certified=true,began=true;unsigned starts=0,cancels=0;std::optional<Witness> result;
    std::string gameVersion() const override {return "fixture-only";}
    std::string path() const override {return "fixture/not-a-game-adapter";}
    bool executionVerified() const override {return certified;}
    bool isolationVerified() const override {return certified;}
    bool recoveryVerified() const override {return certified;}
    bool supports(const std::string& kind,const std::string& op) const override {return kind=="mineral"&&op=="mine";}
    bool validate(const Intent&,const Object&,const Object&) const override {return true;}
    bool begin(const Ticket&) override {++starts;return began;}
    std::optional<Witness> poll(const Ticket&) override {auto out=result;result.reset();return out;}
    void cancel(const Ticket&) noexcept override {++cancels;}
};
int main() {
    Host host;Fixture adapter;host.reset(scene(),"fixture-only");
    host.observe("player",{{"host","player",{{"hp",100},{"max_hp",100},{"dead",false}},10}});
    host.observe("mineral",{mineral()});
    auto actor=host.find("player","host")->ref;auto target=host.find("mineral","ore-12")->ref;
    check(host.bind(2,1,actor),"native player ownership binds an authenticated member incarnation");
    check(!host.bind(3,1,actor),"one native player cannot be owned by two peers");
    Intent intent{scene(),2,1,1,88,1,1,actor,target,"mine"};
    adapter.certified=false;
    check(!host.resume(adapter)&&!host.submit(intent.json(),2,adapter,100),"census cannot authorize an unverified native path");
    adapter.certified=true;check(host.resume(adapter),"fixture-only execution requires isolation and recovery proof");
    auto bad=intent.json();bad["damage"]=999999;
    check(!host.submit(bad,2,adapter,100)&&adapter.starts==0,"claimed damage cannot enter native execution");
    check(!host.submit(intent.json(),3,adapter,100),"transport sender cannot impersonate operator");
    bad=intent.json();bad["scope"]["world"]="ffffffffffffffffffffffffffffffff";
    check(!host.submit(bad,2,adapter,100),"room ID reuse cannot grant an old world permission");
    bad=intent.json();bad["target"]["life"]=99;
    check(!host.submit(bad,2,adapter,100),"stale lifecycle cannot operate a reused object");
    auto ticket=host.submit(intent.json(),2,adapter,100);
    check(ticket&&adapter.starts==1,"one validated intent dispatches once to fixture native adapter");
    check(!host.submit(intent.json(),2,adapter,101),"duplicate request cannot dispatch again");
    auto disguised=intent;disguised.request=2;
    check(!host.submit(disguised.json(),2,adapter,101),"new request ID cannot disguise the same action strike");
    host.poll(200);check(host.status()["results_confirmed"]==0,"dispatch return without native witness is not a result");
    adapter.result=Witness{*ticket,10,20,1,mineral(7500).state};host.poll(201);
    check(host.find(target)->version==2&&host.find(target)->state["hp_ratio"]==7500&&host.status()["results_confirmed"]==1,
        "correlated host native witness commits the observed result exactly once");
    check(host.status()["full_sync_verified"]==false,"fixture tests never certify real full synchronization");
    auto baseline=host.baseline();Replica guest;
    check(!guest.baseline(baseline,scene(),false)&&guest.baseline(baseline,scene(),true),"late join accepts only host unified baseline");
    auto old=guest.version();host.observe("mechanism",{{"lever","mechanism",{{"active",true},{"phase","open"}},30}});
    auto delta=host.changesSince(old);
    check(delta&&guest.delta(*delta,true)&&guest.snapshot().size()==3,"another object kind uses the same ordered journal");
    check(!guest.delta(*delta,true),"duplicate journal delivery cannot reapply state");
    const auto beforeMalformed=guest.snapshot();auto corrupted=host.baseline();corrupted["objects"][0]["account_uid"]=123;
    check(!guest.baseline(corrupted,scene(),true)&&guest.snapshot()==beforeMalformed,"private fields reject whole baseline atomically");
    auto invalid=mineral();invalid.state["hp_ratio"]=-1;auto before=host.baseline();
    check(throws([&]{host.observe("mineral",{invalid});})&&host.baseline()==before,"invalid native census preserves all previous identities");
    check(throws([&]{host.observe("mineral",{mineral(),mineral()});})&&host.baseline()==before,"ambiguous native keys cannot partially replace world");
    auto next=intent;next.request=2;next.strike=2;next.expectedVersion=2;
    auto ticket2=host.submit(next.json(),2,adapter,300);
    host.observe("mineral",{mineral(7000)});adapter.result=Witness{*ticket2,10,20,2,mineral(5000).state};host.poll(350);
    check(host.status()["results_confirmed"]==1&&host.find(target)->state["hp_ratio"]==7000,
        "unrelated background native change cannot acknowledge a pending strike");
    next.request=3;next.strike=3;next.expectedVersion=3;auto ticket3=host.submit(next.json(),2,adapter,400);
    adapter.result=Witness{*ticket3,11,20,3,mineral(4000).state};host.poll(450);
    check(host.status()["results_confirmed"]==1,"another native actor cannot witness this operation");
    next.request=4;next.strike=4;auto ticket4=host.submit(next.json(),2,adapter,500);
    adapter.result=Witness{*ticket4,10,20,1,mineral(4000).state};host.poll(550);
    check(host.status()["results_confirmed"]==1,"one native event cannot confirm different requests");
    next.request=5;next.strike=5;check(host.submit(next.json(),2,adapter,600).has_value(),"new independent request can be pending");
    host.poll(2100);check(host.status()["pending"]==0&&host.status()["expired"]==1,"unobserved native operations time out without results");
    check(!host.submit(next.json(),2,adapter,2101),"timeout does not make an ambiguous request executable again");
    next.request=6;next.strike=6;check(host.submit(next.json(),2,adapter,2200).has_value(),"pending ownership can be revoked");
    host.revoke(2);check(host.status()["pending"]==0&&!host.bind(2,1,actor),"disconnect cancels tickets and old member permissions");
    check(host.bind(2,2,actor),"reconnect requires a new member incarnation");
    check(!host.submit(next.json(),2,adapter,2201),"old member commands cannot survive reconnect");
    auto priorVersion=guest.version();host.observe("mineral",{mineral(10000,21)});
    auto newTarget=host.find("mineral","ore-12")->ref;
    check(newTarget!=target&&host.find(target)==nullptr,"pooled native target receives a new public identity and lifetime");
    check(guest.delta(*host.changesSince(priorVersion),true),"target retirement and replacement use the shared lifecycle journal");
    priorVersion=guest.version();host.observe("mineral",{mineral(0,21,true)});host.observe("mineral",{mineral(10000,21)});
    check(host.find("mineral","ore-12")->ref!=newTarget,"regeneration in the same native allocation revokes old lifecycle");
    auto gap=*host.changesSince(priorVersion);gap["base"]=priorVersion+1;
    check(!guest.delta(gap,true)&&guest.needsBaseline()&&guest.snapshot().empty(),"missing journal interval withdraws replica operations until baseline");
    check(!guest.baseline(baseline,scene(),true),"resync cannot roll back to an older baseline");
    check(guest.baseline(host.baseline(),scene(),true)&&!guest.needsBaseline(),"full host baseline repairs journal gaps");
    auto rollback=host.baseline();rollback["revision"]=guest.version()+1;rollback["objects"][0]["version"]=0;
    check(!guest.baseline(rollback,scene(),true),"invalid state version cannot be hidden in a later world revision");
    check(!guest.baseline(host.baseline(),scene(2),true),"different scene cannot apply a prior baseline");
    std::atomic<bool> wrongThread=false;std::thread other([&]{wrongThread=throws([&]{host.suspend();});});other.join();
    check(wrongThread,"network or worker threads cannot mutate native authority");
    WorldHandoff handoff;
    check(!handoff.begin(scene(),false)&&handoff.begin(scene(),true),"handoff requires retained personal world before shared loading");
    check(!handoff.personalOperationAllowed()&&!handoff.roomOperationAllowed(scene()),"loading permits neither personal nor shared operations");
    adapter.certified=false;check(!handoff.admit(scene(),true,true,adapter,"fixture-only"),"baseline and native initialization do not certify isolation");
    adapter.certified=true;check(handoff.admit(scene(),true,true,adapter,"fixture-only")&&handoff.roomOperationAllowed(scene()),"fixture handoff checks exact world scope and all adapter proofs");
    check(!handoff.roomOperationAllowed(scene(2)),"new scene immediately lacks old operation permission");
    handoff.withdraw();handoff.restored(false);check(handoff.phase()==WorldHandoff::Phase::Failed&&handoff.recoveryRetained()&&!handoff.personalOperationAllowed(),"failed native recovery retains snapshot and closes account operation gate");
    handoff.withdraw();handoff.restored(true);handoff.withdraw();handoff.restored(true);
    check(handoff.personalOperationAllowed()&&!handoff.recoveryRetained(),"observed native restoration is retryable and cleanup idempotent");
    host.reset(scene(2),"fixture-only");host.observe("player",{{"host","player",{{"hp",100},{"max_hp",100}},10}});
    check(host.find("player","host")->ref!=actor&&host.status()["suspended"]==true,"scene reset revokes ownership and retains monotonic allocation");
    check(newWorldIncarnation().size()==32&&newWorldIncarnation()!=newWorldIncarnation(),"room incarnations are independent of official identities");
    for(const auto& kind:{"enemy","destructible","skill","projectile","summon","effect","wave","drop_display"})host.observe(kind,{{kind,kind,{{"active",true}},40}});
    check(host.baseline()["objects"].size()==9,"remaining object kinds share exactly the same ledger without category-specific authority");
    return failures?1:0;
}
