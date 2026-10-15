#include "authority/world_authority.h"
#include "authority/native_build_policy.h"
#include "authority/native_resource_state.h"
#include "authority/native_battle_state.h"
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
    bool certified=true,dualCertified=true,began=true,ownershipValid=true,personalDomain=false;
    unsigned starts=0,cancels=0;std::optional<Witness> result;
    bool retained=false,allowCapture=true,beginWorld=true,initialized=true,applied=false,restoreBegin=true,restoreResult=false;
    unsigned restores=0,releases=0,applies=0;Scope activeScope;std::set<std::uint64_t> observedBaselines;std::uint64_t submittedRevision=0;
    std::string gameVersion() const override {return "fixture-only";}
    std::string path() const override {return "fixture/not-a-game-adapter";}
    bool executionVerified() const override {return certified;}
    bool isolationVerified() const override {return certified;}
    bool recoveryVerified() const override {return certified;}
    bool dualClientVerified() const override {return dualCertified;}
    NativeActorContext actorContext(const Object& actor) const override {
        if(!ownershipValid)return {};
        return {std::uint16_t(actor.key=="other"?3:2),personalDomain?NativeActorDomain::HostPersonal:NativeActorDomain::RoomProxy};
    }
    bool supports(const std::string& kind,const std::string& op) const override {return kind=="mineral"&&op=="mine";}
    bool validate(const Intent&,const Object&,const Object&) const override {return true;}
    bool begin(const Ticket& ticket,const NativeDispatchPermit& permit) override {
        if(!permit.consume(ticket,path()))return false;++starts;return began;
    }
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
struct InputFixture:Fixture {
    Json received;
    bool supports(const std::string&,const std::string&) const override {return true;}
    bool begin(const Ticket& ticket,const NativeDispatchPermit& permit) override {received=ticket.intent.input;return Fixture::begin(ticket,permit);}
};
struct ReentrantFixture:Fixture {
    unsigned polls=0;
    std::function<void()> duringPoll,duringCancel,duringContext;
    NativeActorContext actorContext(const Object& actor) const override {
        auto& self=*const_cast<ReentrantFixture*>(this);auto callback=std::move(self.duringContext);
        if(callback)callback();return Fixture::actorContext(actor);
    }
    std::optional<Witness> poll(const Ticket& ticket) override {
        ++polls;auto callback=std::move(duringPoll);if(callback)callback();return Fixture::poll(ticket);
    }
    void cancel(const Ticket& ticket) noexcept override {
        Fixture::cancel(ticket);auto callback=std::move(duringCancel);if(callback)callback();
    }
};
struct ProbeBudgetFixture:Fixture {
    mutable unsigned ownershipProbes=0;
    NativeActorContext actorContext(const Object& actor) const override {++ownershipProbes;return Fixture::actorContext(actor);}
};
int main() {
    {
        ProbeBudgetFixture adapter;Host host;host.reset(scene(),"fixture-only");
        host.observeFrame({{"player",{{"hero","player",{{"active",true}},10}}},{"mineral",{mineral()}}});
        host.bind(2,1,host.find("player","hero")->ref);host.resume(adapter);
        Intent intent{scene(),2,1,1,1,1,1,host.find("player","hero")->ref,host.find("mineral","ore-12")->ref,"mine"};
        bool admitted=true;for(unsigned i=1;i<=128;++i) {intent.request=i;intent.action=i;admitted=admitted&&host.submit(intent.json(),2,adapter,10).has_value();}
        adapter.ownershipProbes=0;host.poll(11);
        check(admitted&&host.status()["pending"]==128&&adapter.ownershipProbes==256,
            "128 native operations revalidate ownership linearly instead of performing a full native rescan per callback");
        adapter.ownershipProbes=0;host.revoke(2);
        check(host.status()["pending"]==0&&adapter.ownershipProbes==0&&adapter.cancels==128,
            "revoked membership cancels an entire native queue without unnecessary native actor calls");
        host.reset();
    }
    {
        ReentrantFixture adapter;Host host;host.reset(scene(),"fixture-only");
        host.observeFrame({{"player",{{"hero","player",{{"active",true}},10}}},{"mineral",{mineral()}}});
        host.bind(2,1,host.find("player","hero")->ref);host.resume(adapter);
        Intent intent{scene(),2,1,1,1,1,1,host.find("player","hero")->ref,host.find("mineral","ore-12")->ref,"mine"};
        const auto ticket=host.submit(intent.json(),2,adapter,10);const auto stamp=host.captureNativeStamp();host.observe("mineral",{mineral(9000)});
        adapter.result=Witness{*ticket,10,20,1,mineral(9000).state,stamp};adapter.duringPoll=[&]{host.poll(11);};host.poll(11);
        check(adapter.polls==1&&host.status()["results_confirmed"]==1,"nested native polling cannot recursively poll or consume the same callback twice");
        host.reset();
    }
    for(unsigned mode=0;mode<5;++mode) {
        ReentrantFixture adapter;Host host;host.reset(scene(),"fixture-only");
        host.observeFrame({{"player",{{"hero","player",{{"active",true}},10}}},{"mineral",{mineral()}}});
        host.bind(2,1,host.find("player","hero")->ref);host.resume(adapter);
        Intent intent{scene(),2,1,1,1,1,1,host.find("player","hero")->ref,host.find("mineral","ore-12")->ref,"mine"};
        const auto ticket=host.submit(intent.json(),2,adapter,10);check(ticket.has_value(),"reentrant native fixture admits one operation");
        const auto stamp=host.captureNativeStamp();host.observe("mineral",{mineral(9000)});
        adapter.result=Witness{*ticket,10,20,1,mineral(9000).state,stamp};
        if(mode==0)adapter.duringPoll=[&]{host.reset();};
        if(mode==1)adapter.duringPoll=[&]{host.revoke(2);};
        if(mode==2)adapter.duringPoll=[&]{host.observe("mineral",{mineral(8000,21)});};
        if(mode==3)adapter.duringPoll=[&]{host.suspend();};
        if(mode==4)adapter.duringPoll=[&]{adapter.certified=false;};
        host.poll(11);
        check(host.status()["pending"]==0&&host.status()["results_confirmed"]==0&&adapter.cancels==1,
            "native poll cannot acknowledge a ticket revoked reentrantly by world, owner, allocation or certification changes");
        host.reset();
    }
    {
        ReentrantFixture adapter;Host host;host.reset(scene(),"fixture-only");
        host.observeFrame({{"player",{{"hero","player",{{"active",true}},10}}},{"mineral",{mineral()}}});
        host.bind(2,1,host.find("player","hero")->ref);host.resume(adapter);
        Intent intent{scene(),2,1,1,1,1,1,host.find("player","hero")->ref,host.find("mineral","ore-12")->ref,"mine"};
        check(host.submit(intent.json(),2,adapter,10).has_value(),"cancellation fixture has a live native ticket");
        bool reopened=true;adapter.duringCancel=[&]{reopened=host.resume(adapter);host.reset();};host.suspend();
        check(!reopened&&adapter.cancels==1&&host.status()["pending"]==0&&host.status()["suspended"]==true,
            "reset from native cancellation neither cancels twice nor reopens authority during teardown");
    }
    {
        ReentrantFixture adapter;Host host;host.reset(scene(),"fixture-only");
        host.observeFrame({{"player",{{"hero","player",{{"active",true}},10}}},{"mineral",{mineral()}}});
        host.bind(2,1,host.find("player","hero")->ref);host.resume(adapter);
        Intent intent{scene(),2,1,1,1,1,1,host.find("player","hero")->ref,host.find("mineral","ore-12")->ref,"mine"};
        host.submit(intent.json(),2,adapter,10);adapter.duringContext=[&]{host.reset();};
        host.observe("mineral",{mineral(9000)});
        check(adapter.cancels==1&&host.status()["pending"]==0&&host.status()["objects"]==0,
            "native ownership probing may reset the scene without dangling pending iterators");
    }
    {
        Host graph;graph.reset(scene(),"fixture-only");
        Observation caster{"caster","player",{{"active",true}},10};
        Observation cast{"cast","skill",{{"active",true},{"phase","begin"}},20};
        cast.relations["caster"]={"player","caster",10,1};
        Observation projectile{"bolt","projectile",{{"active",true}},30};
        projectile.relations["parent"]={"skill","cast",20,1};projectile.relations["caster"]={"player","caster",10,1};
        Observation effect{"beam","effect",{{"active",true}},40};effect.relations["parent"]={"projectile","bolt",30,1};
        const std::map<std::string,std::vector<Observation>> nativeFrame={{"effect",{effect}},{"player",{caster}},{"projectile",{projectile}},{"skill",{cast}}};
        graph.observeFrame(nativeFrame);
        const auto originalCaster=graph.find("player","caster")->ref,originalCast=graph.find("skill","cast")->ref;
        check(graph.find("projectile","bolt")->relations["parent"]==originalCast.json()&&
              graph.find("effect","beam")->relations["parent"]==graph.find("projectile","bolt")->ref.json(),
            "one native frame resolves caster, cast, projectile and persistent effect across all kinds");
        Replica joined;check(joined.baseline(graph.baseline(),scene(),true)&&joined.snapshot().size()==4,
            "late join receives a complete typed dependency graph");
        const auto prior=graph.baseline();auto forged=graph.baseline();
        forged["objects"][0]["relations"]["parent"]={{"id",9999},{"life",1}};
        check(!joined.baseline(forged,scene(),true),"baseline cannot introduce an unresolved provenance reference");
        auto changedOrigin=graph.baseline();changedOrigin["revision"]=joined.version()+1;
        Json altered;
        for(auto& object:changedOrigin["objects"])if(object["kind"]=="skill") {
            object["version"]=object["version"].get<std::uint64_t>()+1;object["relations"]["origin"]=originalCaster.json();altered=object;
        }
        check(!joined.baseline(changedOrigin,scene(),true),"higher baseline version cannot change a native instance's origin");
        Replica originReplica;originReplica.baseline(graph.baseline(),scene(),true);
        Json originDelta={{"scope",scene().json()},{"base",originReplica.version()},{"revision",originReplica.version()+1},
            {"events",Json::array({{{"scope",scene().json()},{"revision",originReplica.version()+1},{"type","upsert"},{"object",altered},{"receipt",nullptr}}})}};
        check(!originReplica.delta(originDelta,true)&&originReplica.needsBaseline(),"delta cannot disguise a different origin as the next state version");
        auto cyclic=nativeFrame;cyclic["skill"][0].relations["parent"]={"effect","beam",40,1};
        check(throws([&]{graph.observeFrame(cyclic);})&&graph.baseline()==prior,"cyclic native parentage rejects the whole frame atomically");
        auto wrongCaster=nativeFrame;wrongCaster["skill"][0].relations["caster"]={"effect","beam",40,1};
        check(throws([&]{graph.observeFrame(wrongCaster);})&&graph.baseline()==prior,"a visual effect cannot masquerade as a native caster");
        caster.cookie=11;
        check(throws([&]{graph.observe("player",{caster});})&&graph.baseline()==prior,
            "recycling a caster cannot silently attach its old skill graph to the new allocation");
        auto rebound=nativeFrame;rebound["player"]={caster};rebound["skill"][0].relations["caster"].cookie=11;
        rebound["projectile"][0].relations["caster"].cookie=11;
        check(throws([&]{graph.observeFrame(rebound);})&&graph.baseline()==prior,
            "one cast instance cannot switch to a recycled caster even inside an otherwise complete native frame");
        rebound["skill"][0].nativeLife=2;rebound["projectile"][0].nativeLife=2;
        rebound["projectile"][0].relations["parent"].nativeLife=2;rebound["effect"][0].relations["parent"].nativeLife=2;
        const auto oldRevision=joined.version();graph.observeFrame(rebound);
        check(graph.find("player","caster")->ref!=originalCaster&&graph.find("skill","cast")->ref!=originalCast&&
              graph.find("effect","beam")->version==2,"observed new caster and cast lifetimes replace provenance while reparenting only advances state");
        check(joined.delta(*graph.changesSince(oldRevision),true)&&joined.snapshot()==graph.baseline()["objects"],
            "one complete delta atomically retires the old caster and updates all native relations");
        const auto retained=graph.baseline();
        check(throws([&]{graph.observe("skill",{});})&&graph.baseline()==retained,"partial teardown cannot strand live native descendants");
        auto retarget=rebound;retarget["skill"][0].relations["target"]={"player","caster",11,1};graph.observeFrame(retarget);
        auto retargetAgain=retarget;retargetAgain["skill"][0].relations["target"]={"projectile","bolt",30,2};
        const auto sameCast=graph.find("skill","cast")->ref;graph.observeFrame(retargetAgain);
        check(graph.find("skill","cast")->ref==sameCast&&graph.find("skill","cast")->version==3,
            "native retargeting advances the cast state without rebinding its immutable caster");
        graph.observeFrame({{"effect",{}},{"projectile",{}},{"skill",{}},{"player",{caster}}});
        check(graph.baseline()["objects"].size()==1,"shared teardown retires an entire native dependency subtree");
        auto oldDelta=graph.changesSince(joined.version());oldDelta->at("events").erase(oldDelta->at("events").begin());
        check(!joined.delta(*oldDelta,true)&&joined.needsBaseline(),"an incomplete dependency delta withdraws the whole replica graph");
        check(graph.baseline().dump().find("cookie")==std::string::npos,"public graph references do not serialize native allocation anchors");
        graph.reset();
    }
    NativeBattleReading battle{"enemy","boss","enemy_template",9,{1,2,3},700,1000,25,100,true};
    auto battleFact=nativeBattleObservation(battle);
    check(battleFact.state["hp"]==700&&battleFact.state["poise"]==25&&battleFact.state["max_poise"]==100,
        "native battle facts preserve actual HP and poise without guest numerical claims");
    battle.poise=battle.maxPoise=0;
    check(!nativeBattleObservation(battle).state.contains("poise"),"actors without native poise capacity do not acquire an invented bar");
    battle.hp=1001;check(throws([&]{nativeBattleObservation(battle);}),"inconsistent native battle values fail instead of being clamped");
    auto rounded=battle;rounded.hp=4412.32737219;rounded.maxHp=4412.327372185988;
    const auto roundedFact=nativeBattleObservation(rounded);
    check(roundedFact.state["hp"].get<double>()==rounded.hp&&roundedFact.state["max_hp"].get<double>()==rounded.maxHp,
        "observed native HP rounding preserves both original values through validation");
    rounded.hp=rounded.maxHp+.001;
    check(throws([&]{nativeBattleObservation(rounded);}),"floating precision tolerance cannot accept a material HP excess");
    rounded.hp=2e-12;rounded.maxHp=1e-12;
    check(throws([&]{nativeBattleObservation(rounded);}),"absolute precision tolerance cannot overwhelm a tiny capacity");
    rounded.hp=700;rounded.maxHp=1000;rounded.poise=std::nextafter(100.f,101.f);rounded.maxPoise=100;
    check(nativeBattleObservation(rounded).state["poise"].get<double>()==rounded.poise,
        "native Single poise rounding is preserved without clipping the observation");
    check(!validState({{"hp_ratio",10001},{"max_hp_ratio",10000}}),"integer mineral durability never acquires a floating precision tolerance");
    battle.hp=700;battle.kind="drop_display";
    check(throws([&]{nativeBattleObservation(battle);}),"battle observation cannot masquerade as another unadapted native kind");
    check(Intent::validInput("cast",{{"slot",3},{"phase","press"}})&&
          Intent::validInput("attack",{{"button","primary"},{"phase","release"}})&&
          Intent::validInput("interact",{{"choice",0}}),"native controls select host-resolved slots, buttons and interaction choices");
    check(!Intent::validInput("cast",{{"slot",4},{"phase","press"}})&&
          !Intent::validInput("cast",{{"slot",-1},{"phase","press"}})&&
          !Intent::validInput("cast",{{"slot",0},{"phase","press"},{"skill","untrusted/path"}})&&
          !Intent::validInput("interact",{{"choice",0},{"reward",100}}),"controls cannot smuggle skill names, rewards or invalid selections");
    Json movement={{"axes",{.6,.8}},{"heading",1.2},{"mode","run"},{"duration_ms",50}};
    check(Intent::validInput("move",movement),"movement is bounded input instead of a guest-provided final position");
    auto invalidMove=movement;invalidMove["axes"]={1,1};
    check(!Intent::validInput("move",invalidMove),"diagonal movement cannot amplify the allowed input magnitude");
    invalidMove=movement;invalidMove["duration_ms"]=1000;
    check(!Intent::validInput("move",invalidMove),"one input cannot hold native movement indefinitely");
    invalidMove=movement;invalidMove["position"]={100,200,300};
    check(!Intent::validInput("move",invalidMove)&&!Intent::validInput("mine",{{"hp_ratio",0}}),"movement and mining inputs cannot contain claimed outcomes");
    {
        Host inputs;InputFixture native;inputs.reset(scene(),"fixture-only");
        inputs.observe("player",{{"own","player",{{"hp",100}},10},{"companion","player",{{"hp",100}},11},{"other","player",{{"hp",100}},12}});
        inputs.observe("mineral",{mineral()});
        auto own=inputs.find("player","own")->ref,companion=inputs.find("player","companion")->ref,other=inputs.find("player","other")->ref;
        inputs.bindGroup(2,1,{own,companion});inputs.bind(3,1,other);inputs.resume(native);
        Intent move{scene(),2,1,1,1,1,1,own,own,"move",movement};
        check(inputs.submit(move.json(),2,native,100)&&native.received==movement,"validated controls reach the compiled adapter unchanged");
        move.request=2;move.action=2;move.target=other;
        check(!inputs.submit(move.json(),2,native,101)&&native.starts==1,"movement cannot operate another player's actor");
        move.target=companion;move.operation="select";move.input=Json::object();
        check(inputs.submit(move.json(),2,native,102).has_value(),"selection stays within the host-granted native squad");
        move.request=3;move.action=3;move.target=other;
        check(!inputs.submit(move.json(),2,native,103),"selection cannot take over another member's squad");
        move.target=own;move.operation="cast";move.input={{"slot",2},{"phase","press"}};
        check(inputs.submit(move.json(),2,native,104)&&native.received==move.input,"cast slot is an input rather than a guest-asserted skill result");
        inputs.reset();
    }
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
    adapter.certified=true;adapter.dualCertified=false;
    check(!host.resume(adapter)&&!host.submit(intent.json(),2,adapter,100),"native execution remains closed before two-client certification");
    adapter.dualCertified=true;
    adapter.certified=true;check(host.resume(adapter),"fixture-only execution requires isolation and recovery proof");
    adapter.personalDomain=true;
    check(!host.submit(intent.json(),2,adapter,100),"a room grant cannot let a guest operate a host personal actor");
    adapter.personalDomain=false;adapter.ownershipValid=false;
    check(!host.submit(intent.json(),2,adapter,100),"room membership cannot replace missing native actor ownership");
    adapter.ownershipValid=true;
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
    const auto firstEvent=host.captureNativeStamp();
    adapter.result=Witness{*ticket,10,20,1,mineral(7500).state,firstEvent};host.poll(201);
    check(host.status()["awaiting_native_census"]==1&&host.find(target)->state["hp_ratio"]==10000,
        "an early native callback waits without manufacturing durability before the next census");
    host.observe("mineral",{mineral(7500)});host.poll(202);
    check(host.find(target)->version==2&&host.find(target)->state["hp_ratio"]==7500&&host.status()["results_confirmed"]==1,
        "correlated witness only acknowledges state already committed by the host native census");
    check(host.status()["full_sync_verified"]==false,"fixture tests never certify real full synchronization");
    {
        Host forged;Fixture native;forged.reset(scene(),"fixture-only");
        forged.observe("player",{{"operator","player",{{"hp",100},{"max_hp",100}},10}});
        forged.observe("mineral",{mineral()});
        const auto owner=forged.find("player","operator")->ref,ore=forged.find("mineral","ore-12")->ref;
        forged.bind(2,1,owner);forged.resume(native);
        Intent command{scene(),2,1,1,1,1,1,owner,ore,"mine"};
        const auto first=forged.submit(command.json(),2,native,10);
        native.result=Witness{*first,10,20,1,mineral().state,forged.captureNativeStamp()};forged.poll(11);
        check(forged.status()["results_confirmed"]==0&&forged.status()["awaiting_native_census"]==1&&
              forged.find(ore)->state["hp_ratio"]==10000,
            "an event receipt without a subsequent host native census stays pending and cannot confirm a result");
        command.request=2;command.action=2;command.strike=2;
        const auto second=forged.submit(command.json(),2,native,12);
        const auto callbackStamp=forged.captureNativeStamp();forged.observe("mineral",{mineral(7500)});
        native.result=Witness{*second,10,20,2,mineral(5000).state,callbackStamp};forged.poll(13);
        check(forged.status()["results_confirmed"]==0&&forged.find(ore)->state["hp_ratio"]==7500,
            "an adapter cannot replace a committed host observation with its claimed durability");
        forged.poll(2000);
        check(forged.status()["pending"]==0&&forged.status()["expired"]==2&&forged.find(ore)->state["hp_ratio"]==7500,
            "cached callbacks expire without changing host-observed state when no matching census arrives");
        forged.reset();
    }
    {
        Host revoked;Fixture native;revoked.reset(scene(),"fixture-only");
        revoked.observe("player",{{"operator","player",{{"hp",100},{"max_hp",100}},10}});
        revoked.observe("mineral",{mineral()});
        const auto owner=revoked.find("player","operator")->ref,ore=revoked.find("mineral","ore-12")->ref;
        revoked.bind(2,1,owner);revoked.resume(native);
        Intent command{scene(),2,1,1,1,1,1,owner,ore,"mine"};
        check(revoked.submit(command.json(),2,native,10).has_value(),"native-owned room actor may start an authority operation");
        native.ownershipValid=false;revoked.poll(11);
        check(revoked.status()["pending"]==0&&native.cancels==1&&revoked.status()["results_confirmed"]==0,
            "losing the live native actor binding revokes a pending operation before confirmation");
        revoked.reset();
    }
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
    auto sameVersionChanged=host.baseline();sameVersionChanged["revision"]=guest.version()+1;
    sameVersionChanged["objects"][0]["state"]["hp_ratio"]=6000;
    check(!guest.baseline(sameVersionChanged,scene(),true)&&guest.snapshot()==beforeMalformed,
        "a later world revision cannot replace state while reusing the same object version");
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
    priorVersion=guest.version();host.observe("mineral",{mineral(0,21,true)});
    auto regenerated=mineral(10000,21);regenerated.nativeLife=2;host.observe("mineral",{regenerated});
    check(host.find("mineral","ore-12")->ref!=newTarget,"observed native lifecycle change in the same allocation revokes old lifecycle");
    auto currentMineral=host.baseline();
    check(throws([&]{host.observe("mineral",{mineral(9000,21)});})&&host.baseline()==currentMineral,
        "an older native lifetime cannot replace the current allocation state");
    {
        Host transitions;Fixture native;transitions.reset(scene(),"fixture-only");
        Observation player{"character","player",{{"hp",100},{"max_hp",100},{"dead",false}},10};
        Observation mechanism{"door","mechanism",{{"active",true},{"phase","open"}},11};
        transitions.observe("player",{player});transitions.observe("mechanism",{mechanism});
        const auto originalPlayer=transitions.find("player","character")->ref,originalDoor=transitions.find("mechanism","door")->ref;
        transitions.bind(2,1,originalPlayer);transitions.resume(native);
        player.state["hp"]=0;player.state["dead"]=true;transitions.observe("player",{player});
        player.state["hp"]=100;player.state["dead"]=false;transitions.observe("player",{player});
        mechanism.state["active"]=false;transitions.observe("mechanism",{mechanism});
        mechanism.state["active"]=true;transitions.observe("mechanism",{mechanism});
        check(transitions.find("player","character")->ref==originalPlayer&&transitions.find("mechanism","door")->ref==originalDoor,
            "revival and mechanism reactivation preserve their native allocation identities");
        check(transitions.bind(2,1,originalPlayer),"native revival does not silently discard squad ownership");
        player.nativeLife=2;transitions.observe("player",{player});
        check(!transitions.find(originalPlayer)&&transitions.find("player","character")->ref!=originalPlayer,
            "explicit native lifecycle replacement uses the same rules for players and minerals");
        transitions.reset();
    }
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
    concurrent.observe("mineral",{mineral(7000)});
    ordered.callbacks.emplace(*concurrentSecond,Witness{*concurrentSecond,10,20,12,mineral(7000).state,laterCapture});concurrent.poll(5);
    ordered.callbacks.emplace(*concurrentFirst,Witness{*concurrentFirst,10,20,11,mineral(8000).state,early});concurrent.poll(6);
    auto concurrencyDelta=concurrent.changesSince(concurrentRevision);
    check(concurrent.status()["results_confirmed"]==1&&concurrent.find(oreRef)->state["hp_ratio"]==7000&&concurrent.find(oreRef)->version==3,
        "late callback claiming an older result cannot roll back the newest host native observation");
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
    check(concurrent.status()["results_confirmed"]==1&&concurrent.find(oreRef)->state["hp_ratio"]==7000,
        "unissued future callback stamp cannot certify or replace native state");
    const auto privateWire=concurrent.baseline().dump();
    check(privateWire.find("nativeStamp")==std::string::npos&&privateWire.find("native_stamp")==std::string::npos&&
        privateWire.find("nativeLife")==std::string::npos&&privateWire.find("cookie")==std::string::npos,
        "native capture order and identities remain private to the host");
    Host squadHost;Fixture squadAdapter;squadHost.reset(scene(),"fixture-only");
    std::vector<Observation> nativeSquad;std::vector<Ref> squad;
    for(unsigned slot=0;slot<5;++slot)nativeSquad.push_back({"slot-"+std::to_string(slot),"player",{{"active",true}},100+slot});
    squadHost.observe("player",nativeSquad);squadHost.observe("mineral",{mineral()});
    for(unsigned slot=0;slot<4;++slot)squad.push_back(squadHost.find("player","slot-"+std::to_string(slot))->ref);
    check(squadHost.bindGroup(2,7,squad),"one authenticated member can own all four native party actors");
    auto oversizedSquad=squad;oversizedSquad.push_back(squadHost.find("player","slot-4")->ref);
    check(!squadHost.bindGroup(3,1,oversizedSquad)&&!squadHost.bindGroup(3,1,{squad.back()}),
        "oversized or overlapping actor grants fail atomically without stealing another member's actors");
    check(!squadHost.replaceActors(2,7,{squad.front(),squad.front()})&&!squadHost.replaceActors(2,6,{squad.front()}),
        "duplicate actors and old member generations cannot replace a native squad");
    squadHost.resume(squadAdapter);
    const auto squadOre=squadHost.find("mineral","ore-12")->ref;
    Intent companion{scene(),2,7,1,1,1,1,squad.back(),squadOre,"mine"};
    check(squadHost.submit(companion.json(),2,squadAdapter,1).has_value(),"a verified fourth party actor can originate an operation");
    auto primary=companion;primary.request=2;primary.actor=squad.front();
    check(squadHost.submit(primary.json(),2,squadAdapter,2).has_value(),
        "separate party actors cannot merge strikes just because their action identifiers match");
    auto retained=squad;retained.pop_back();
    check(squadHost.replaceActors(2,7,retained)&&squadHost.status()["pending"]==1&&squadAdapter.cancels==1,
        "native party replacement revokes only tickets belonging to removed actors");
    primary.request=1;primary.strike=2;
    check(!squadHost.submit(primary.json(),2,squadAdapter,3),"party replacement does not reset member request deduplication");
    companion.request=3;companion.strike=2;
    check(!squadHost.submit(companion.json(),2,squadAdapter,3),"removed live actor cannot operate merely because its native object still exists");
    squadHost.revoke(2);check(squadHost.status()["pending"]==0&&!squadHost.replaceActors(2,7,squad),
        "disconnect revokes every party actor and cannot be undone by a replacement grant");
    squadHost.reset();
    Host longHost;Fixture longAdapter;longHost.reset(scene(),"fixture-only");
    longHost.observe("player",{{"operator","player",{{"hp",100},{"max_hp",100}},10}});
    longHost.observe("mineral",{mineral()});
    const auto longActor=longHost.find("player","operator")->ref,longOre=longHost.find("mineral","ore-12")->ref;
    longHost.bind(2,1,longActor);longHost.resume(longAdapter);
    bool continuous=true;
    for(std::uint64_t index=1;index<=10020;++index) {
        if(index==8193) {
            Intent boundary{scene(),2,1,index,1,2,1,longActor,longOre,"mine"};
            check(!longHost.submit(boundary.json(),2,longAdapter,index*2),"history eviction cannot dispatch the old action that it has just retired");
        }
        Intent input{scene(),2,1,index+(index>=8193?1:0),index,1,1,longActor,longOre,"mine"};
        const auto longTicket=longHost.submit(input.json(),2,longAdapter,index*2);
        if(!longTicket) {continuous=false;break;}
        const auto callbackStamp=longHost.captureNativeStamp();
        longHost.observe("mineral",{mineral()});
        longAdapter.result=Witness{*longTicket,10,20,index,mineral().state,callbackStamp};
        longHost.poll(index*2+1);
    }
    check(continuous&&longHost.status()["results_confirmed"]==10020,"a long session does not stop when command/event dedup reaches its memory window");
    check(longHost.status()["dedup_commands"]==8192&&longHost.status()["dedup_event_paths"]==1,"long-session command and event history remains bounded");
    Intent retired{scene(),2,1,10022,1,2,1,longActor,longOre,"mine"};
    check(!longHost.submit(retired.json(),2,longAdapter,21000),"eviction cannot revive an old command with a new request number");
    auto newest=retired;newest.action=10021;newest.strike=1;
    const auto oldEventTicket=longHost.submit(newest.json(),2,longAdapter,21000);
    check(oldEventTicket.has_value(),"new input continues after old commands retire");
    if(oldEventTicket) {
        const auto callbackStamp=longHost.captureNativeStamp();longHost.observe("mineral",{mineral()});
        longAdapter.result=Witness{*oldEventTicket,10,20,1,mineral().state,callbackStamp};
        longHost.poll(21001);
        check(longHost.status()["results_confirmed"]==10020&&longHost.status()["pending"]==0,"event window rejects an evicted old native event even with a fresh capture stamp");
    }
    longHost.reset();
    return failures?1:0;
}
