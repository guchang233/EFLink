#include "authority/world_authority.h"
#include "authority/native_build_policy.h"
#include "authority/native_resource_state.h"
#include "authority/native_trace_window.h"
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
    bool retained=false,allowCapture=true,beginWorld=true,initialized=true,applied=false,restoreBegin=true,restoreResult=false;
    unsigned restores=0,releases=0,applies=0;Scope activeScope;std::set<std::uint64_t> observedBaselines;std::uint64_t submittedRevision=0;
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
    std::optional<std::uint64_t> capturePersonal() override {if(!allowCapture)return {};retained=true;return 99;}
    bool personalRetained(std::uint64_t handle) const override {return handle==99&&retained;}
    bool beginShared(std::uint64_t,const Scope& scope) override {activeScope=scope;return beginWorld;}
    bool nativeInitialized(const Scope& scope) const override {return initialized&&scope==activeScope;}
    bool applyBaseline(const Scope&,std::uint64_t revision,const Json&) override {++applies;submittedRevision=revision;return true;}
    bool baselineObserved(const Scope&,std::uint64_t revision) const override {return applied||observedBaselines.contains(revision);}
    bool beginRestore(std::uint64_t) override {++restores;return restoreBegin;}
    bool restoreObserved(std::uint64_t) const override {return restoreResult;}
    void releasePersonal(std::uint64_t) noexcept override {++releases;retained=false;}
};
struct PerTicketFixture:Fixture {
    std::map<std::uint64_t,Witness> callbacks;
    std::optional<Witness> poll(const Ticket& ticket) override {
        auto found=callbacks.find(ticket.serial);if(found==callbacks.end())return {};
        auto value=found->second;callbacks.erase(found);return value;
    }
};
int main() {
    NativeTraceWindow trace;
    trace.singleAttack();check(trace.active(1000000)&&trace.awaitingAttack()&&!trace.collecting(1000000),
        "single native capture waits for a delayed human operation without collecting idle state");
    trace.nativeAttack(1000000);trace.nativeAttack(1002000);
    check(trace.collecting(1002999)&&!trace.active(1003000),"following party hits cannot extend one-shot capture indefinitely");
    trace.singleAttack();trace.stop();trace.nativeAttack(2000000);
    check(!trace.active(2000000),"native callback after diagnostic stop cannot restart capture");
    trace.timed(5,1000);trace.nativeAttack(200);
    check(trace.active(1004)&&!trace.active(1005)&&!trace.awaitingAttack(),"timed capture preserves its original bound");
    trace.singleAttack(30000);trace.nativeAttack(10);trace.nativeAttack(30000);
    check(trace.collecting(30009)&&!trace.active(30010)&&throws([&]{trace.singleAttack(30001);}),
        "resource completion capture has a finite bound that subsequent hits cannot extend");
    MineralReading reading{"ore","quartz",{1,2,3},9,1490,1356,10000,false};
    const auto committed=mineralObservation(reading);
    check(committed.state["hp_ratio"]==1490&&!committed.state.contains("runtime_hp_ratio")&&mineralReadback(reading)["runtime_diverged"]==true,
        "native runtime prediction stays diagnostic and cannot replace property durability");
    reading.runtimeHp=0;check(mineralObservation(reading).state["collected"]==false,
        "zero predicted runtime HP cannot manufacture resource destruction");
    reading.propertyHp=0;check(mineralObservation(reading).state["collected"]==false,
        "zero property durability cannot invent a not-yet-observed native collection flag");
    reading.propertyHp=-1;check(throws([&]{mineralObservation(reading);}),"invalid native property value is rejected rather than clamped to a fabricated result");
    check(readOnlyBuildSupported("1.5.3")&&!readOnlyBuildSupported("")&&!readOnlyBuildSupported("1.5.4")&&!readOnlyBuildSupported("fixture-only"),
        "native read-only hooks default off for unknown game builds");
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
    adapter.result=Witness{*ticket,10,20,1,mineral(7500).state,host.captureNativeStamp()};host.poll(201);
    check(host.find(target)->version==2&&host.find(target)->state["hp_ratio"]==7500&&host.status()["results_confirmed"]==1,
        "correlated host native witness commits the observed result exactly once");
    check(host.status()["full_sync_verified"]==false,"fixture tests never certify real full synchronization");
    auto baseline=host.baseline();Replica guest;
    check(!guest.baseline(baseline,scene(),false)&&guest.baseline(baseline,scene(),true),"late join accepts only host unified baseline");
    auto old=guest.version();host.observe("mechanism",{{"lever","mechanism",{{"active",true},{"phase","open"}},30}});
    auto delta=host.changesSince(old);
    check(delta&&guest.delta(*delta,true)&&guest.snapshot().size()==3,"another object kind uses the same ordered journal");
    check(!guest.delta(*delta,true),"duplicate journal delivery cannot reapply state");
    check(!validReceipt(Json{{"peer",2},{"member",1},{"request",1},{"action",88},{"strike",1},{"reward",999}}),
        "result receipts cannot carry rewards or private account fields");
    const auto beforeMalformed=guest.snapshot();auto corrupted=host.baseline();corrupted["objects"][0]["account_uid"]=123;
    check(!guest.baseline(corrupted,scene(),true)&&guest.snapshot()==beforeMalformed,"private fields reject whole baseline atomically");
    auto invalid=mineral();invalid.state["hp_ratio"]=-1;auto before=host.baseline();
    check(throws([&]{host.observe("mineral",{invalid});})&&host.baseline()==before,"invalid native census preserves all previous identities");
    check(throws([&]{host.observe("mineral",{mineral(),mineral()});})&&host.baseline()==before,"ambiguous native keys cannot partially replace world");
    auto next=intent;next.request=2;next.strike=2;next.expectedVersion=2;
    auto preDispatchStamp=host.captureNativeStamp();auto ticket2=host.submit(next.json(),2,adapter,300);
    host.observe("mineral",{mineral(7000)});host.poll(340);
    check(host.status()["pending"]==1&&host.status()["results_confirmed"]==1,
        "background native census alone cannot acknowledge a pending operation");
    adapter.result=Witness{*ticket2,10,20,2,mineral(5000).state,preDispatchStamp};host.poll(350);
    check(host.status()["results_confirmed"]==1&&host.find(target)->state["hp_ratio"]==7000,
        "callback captured before native dispatch cannot acknowledge a later strike");
    next.request=3;next.strike=3;next.expectedVersion=3;auto ticket3=host.submit(next.json(),2,adapter,400);
    adapter.result=Witness{*ticket3,11,20,3,mineral(4000).state,host.captureNativeStamp()};host.poll(450);
    check(host.status()["results_confirmed"]==1,"another native actor cannot witness this operation");
    next.request=4;next.strike=4;auto ticket4=host.submit(next.json(),2,adapter,500);
    adapter.result=Witness{*ticket4,10,20,1,mineral(4000).state,host.captureNativeStamp()};host.poll(550);
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
    adapter.allowCapture=false;
    check(!handoff.begin(scene(),adapter,"fixture-only"),"handoff cannot begin without a retained native personal snapshot");
    adapter.allowCapture=true;
    check(handoff.begin(scene(),adapter,"fixture-only"),"handoff captures a private native snapshot before shared loading");
    check(!handoff.personalOperationAllowed()&&!handoff.roomOperationAllowed(scene()),"loading permits neither personal nor shared operations");
    adapter.certified=false;check(!handoff.admit(guest,"fixture-only"),"baseline and native initialization do not certify isolation");
    adapter.certified=true;check(!handoff.admit(guest,"fixture-only")&&!handoff.roomOperationAllowed(scene())&&adapter.applies==1,
        "native baseline dispatch return cannot admit a guest before observed application");
    adapter.applied=true;check(handoff.admit(guest,"fixture-only")&&handoff.roomOperationAllowed(scene()),"fixture handoff checks exact world scope and all native observations");
    check(!handoff.roomOperationAllowed(scene(2)),"new scene immediately lacks old operation permission");
    Fixture catchingUp;WorldHandoff catchup;
    check(catchup.begin(scene(),catchingUp,"fixture-only")&&!catchup.admit(guest,"fixture-only"),
        "native admission waits for initial baseline application");
    const auto loadingRevision=guest.version();
    host.observe("mechanism",{{"lever","mechanism",{{"active",true},{"phase","closing"}},30}});
    check(guest.delta(*host.changesSince(loadingRevision),true)&&!catchup.admit(guest,"fixture-only")&&catchingUp.applies==1,
        "new journal states during loading cannot overlap a pending native baseline");
    catchingUp.observedBaselines.insert(loadingRevision);
    check(!catchup.admit(guest,"fixture-only")&&catchingUp.applies==2&&catchingUp.submittedRevision==guest.version(),
        "observed old baseline triggers one catch-up application instead of permanent loading");
    check(!catchup.admit(guest,"fixture-only")&&catchingUp.applies==2&&!catchup.roomOperationAllowed(scene()),
        "catch-up dispatch alone never opens shared operations");
    catchingUp.observedBaselines.insert(guest.version());
    check(catchup.admit(guest,"fixture-only"),"admission opens only after the latest native baseline is observed");
    catchup.withdraw();catchingUp.restoreResult=true;catchup.restore();
    handoff.withdraw();adapter.restoreBegin=false;
    check(!handoff.restore()&&handoff.phase()==WorldHandoff::Phase::Failed&&handoff.recoveryRetained()&&!handoff.personalOperationAllowed(),"failed native recovery retains snapshot and closes account operation gate");
    handoff.withdraw();adapter.restoreBegin=true;
    check(!handoff.restore()&&!handoff.restore()&&adapter.restores==2&&handoff.recoveryRetained(),"native restore return is not success and polling cannot repeat restoration dispatch");
    adapter.restoreResult=true;check(handoff.restore(),"observed native restoration releases retained private snapshot");handoff.withdraw();handoff.restore();
    check(handoff.personalOperationAllowed()&&!handoff.recoveryRetained()&&adapter.releases==1,"observed native restoration is retryable and cleanup idempotent");
    WorldHandoff failedBegin;adapter.beginWorld=false;
    check(!failedBegin.begin(scene(),adapter,"fixture-only")&&failedBegin.phase()==WorldHandoff::Phase::Restoring&&failedBegin.recoveryRetained()&&
        !failedBegin.personalOperationAllowed(),"partial native scene entry failure must restore rather than discard personal snapshot");
    failedBegin.restore();
    wrongThread=false;std::thread handoffWorker([&]{wrongThread=throws([&]{handoff.withdraw();});});handoffWorker.join();
    check(wrongThread,"worker/network threads cannot execute native handoff or recovery");
    host.reset(scene(2),"fixture-only");host.observe("player",{{"host","player",{{"hp",100},{"max_hp",100}},10}});
    check(host.find("player","host")->ref!=actor&&host.status()["suspended"]==true,"scene reset revokes ownership and retains monotonic allocation");
    check(newWorldIncarnation().size()==32&&newWorldIncarnation()!=newWorldIncarnation(),"room incarnations are independent of official identities");
    for(const auto& kind:{"enemy","destructible","skill","projectile","summon","effect","wave","drop_display"})host.observe(kind,{{kind,kind,{{"active",true}},40}});
    check(host.baseline()["objects"].size()==9,"remaining object kinds share exactly the same ledger without category-specific authority");
    auto retainedRevision=host.baseline().at("revision").get<std::uint64_t>();
    for(unsigned stage=0;stage<300;++stage)host.observe("mechanism",{{"bounded","mechanism",{{"stage",stage}},101}});
    check(!host.changesSince(retainedRevision),"history beyond bounded journal retention requires complete baseline");
    Replica late;check(late.baseline(host.baseline(),scene(2),true)&&late.snapshot().size()==10,
        "late join restores every object kind after incremental history expires");
    auto beforeOversize=host.baseline();std::vector<Observation> oversized;
    for(unsigned index=0;index<Host::maxObjects+1;++index)oversized.push_back({std::to_string(index),"effect",{{"active",true}},index+1});
    check(throws([&]{host.observe("effect",oversized);})&&host.baseline()==beforeOversize,
        "oversized census cannot evict valid world state or exceed transport capacity");
    Host concurrent;PerTicketFixture ordered;concurrent.reset(scene(),"fixture-only");
    concurrent.observe("player",{{"guest","player",{{"active",true}},10}});concurrent.observe("mineral",{mineral()});
    const auto operatorRef=concurrent.find("player","guest")->ref,oreRef=concurrent.find("mineral","ore-12")->ref;
    concurrent.bind(2,1,operatorRef);concurrent.resume(ordered);
    Intent first{scene(),2,1,1,1,1,1,operatorRef,oreRef,"mine"};
    concurrent.observe("mineral",{mineral(9500)});
    auto concurrentFirst=concurrent.submit(first.json(),2,ordered,1);
    check(concurrentFirst.has_value(),"network-delayed intent can validate against newer state of the same live target");
    auto future=first;future.request=2;future.strike=2;future.expectedVersion=99;
    check(!concurrent.submit(future.json(),2,ordered,1),"intent cannot reference an unobserved future target version");
    auto second=first;second.request=2;second.strike=2;
    auto concurrentSecond=concurrent.submit(second.json(),2,ordered,2);
    Replica concurrentGuest;concurrentGuest.baseline(concurrent.baseline(),scene(),true);auto concurrentRevision=concurrentGuest.version();
    const auto precedingBaseline=concurrent.baseline();
    const auto early=concurrent.captureNativeStamp(),laterCapture=concurrent.captureNativeStamp();
    ordered.callbacks.emplace(*concurrentSecond,Witness{*concurrentSecond,10,20,12,mineral(7000).state,laterCapture});concurrent.poll(5);
    ordered.callbacks.emplace(*concurrentFirst,Witness{*concurrentFirst,10,20,11,mineral(8000).state,early});concurrent.poll(6);
    auto concurrencyDelta=concurrent.changesSince(concurrentRevision);
    check(concurrent.status()["results_confirmed"]==2&&concurrent.find(oreRef)->state["hp_ratio"]==7000&&concurrent.find(oreRef)->version==3,
        "late correlated callback acknowledges its strike without rolling back a newer native result");
    Replica finalBaseline;finalBaseline.baseline(concurrent.baseline(),scene(),true);
    check(concurrencyDelta&&concurrentGuest.delta(*concurrencyDelta,true)&&concurrentGuest.snapshot()==finalBaseline.snapshot(),
        "state-preserving receipts share the ordered journal and converge on the newest native state");
    auto malformedReceipt=*concurrencyDelta;malformedReceipt["events"].back()["object"]["state"]["hp_ratio"]=9000;
    Replica receiptStart;
    check(receiptStart.baseline(precedingBaseline,scene(),true)&&!receiptStart.delta(malformedReceipt,true)&&receiptStart.needsBaseline(),
        "a receipt with different canonical state is rejected rather than used as a state update");
    auto unissued=first;unissued.request=3;unissued.strike=3;
    const auto unissuedTicket=concurrent.submit(unissued.json(),2,ordered,7);
    ordered.callbacks.emplace(*unissuedTicket,Witness{*unissuedTicket,10,20,13,mineral(6000).state,UINT64_MAX});concurrent.poll(8);
    check(concurrent.status()["results_confirmed"]==2&&concurrent.find(oreRef)->state["hp_ratio"]==7000,
        "unissued future callback stamp cannot certify or replace native state");
    const auto privateWire=concurrent.baseline().dump();
    check(privateWire.find("nativeStamp")==std::string::npos&&privateWire.find("native_stamp")==std::string::npos&&privateWire.find("cookie")==std::string::npos,
        "native capture order and identities remain private to the host");
    return failures?1:0;
}
