// Transport/adapter integration fixture, NOT real-game certification.
#include "link_session.h"
#include <iostream>
using namespace eflink;
using namespace eflink::worldauthority;
static void check(bool value,const char* name) {if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<std::endl;}
template<class F> static bool wait(F f) {for(int i=0;i<200;++i) {if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(15));}return f();}
struct Fixture final:NativeAdapter {
    Session* session=nullptr;bool certified=true,saved=false,restoreReady=false,restoreStarted=false,acceptCast=false;
    Json lastInput;
    std::uint64_t revision=0,event=0;unsigned begins=0,cancels=0;std::optional<Witness> result;
    std::string gameVersion() const override{return "fixture-game";}
    std::string path() const override{return "transport-fixture-only";}
    bool executionVerified() const override{return certified;}
    bool isolationVerified() const override{return certified;}
    bool recoveryVerified() const override{return certified;}
    bool supports(const std::string& kind,const std::string& op) const override{return kind=="mineral"&&(op=="mine"||(acceptCast&&op=="cast"));}
    bool validate(const Intent&,const Object& actor,const Object& target) const override{return actor.kind=="player"&&target.kind=="mineral";}
    bool begin(const Ticket& ticket) override {
        ++begins;
        lastInput=ticket.intent.input;
        result=Witness{ticket.serial,ticket.actorCookie,ticket.targetCookie,++event,
            {{"template","ore"},{"hp_ratio",9000},{"max_hp_ratio",10000},{"collected",false}},session->captureNativeResultStamp()};
        return true;
    }
    std::optional<Witness> poll(const Ticket&) override {auto value=result;result.reset();return value;}
    void cancel(const Ticket&) noexcept override{++cancels;result.reset();}
    std::optional<std::uint64_t> capturePersonal() override{saved=true;return 11;}
    bool personalRetained(std::uint64_t handle) const override{return saved&&handle==11;}
    bool beginShared(std::uint64_t,const Scope&) override{return true;}
    bool nativeInitialized(const Scope&) const override{return true;}
    bool applyBaseline(const Scope&,std::uint64_t value,const Json&) override{revision=value;return true;}
    bool baselineObserved(const Scope&,std::uint64_t value) const override{return revision==value;}
    bool beginRestore(std::uint64_t) override{restoreStarted=true;return true;}
    bool restoreObserved(std::uint64_t) const override{return restoreReady;}
    void releasePersonal(std::uint64_t) noexcept override{saved=false;}
};
int run() {
    Net host,guest;Session h,g;Fixture ha,ga;ha.session=&h;ga.session=&g;
    host.setName("authority-host");guest.setName("authority-guest");
    h.attachNativeAdapter(&ha);g.attachNativeAdapter(&ga);h.setAdapterVersion("fixture-game");g.setAdapterVersion("fixture-game");
    host.onData=[&](auto peer,auto channel,auto data,auto count){if(channel==worldChannel)h.receive(peer,data,count);};
    guest.onData=[&](auto peer,auto channel,auto data,auto count){if(channel==worldChannel)g.receive(peer,data,count);};
    check(host.start(48901)&&guest.start(48902),"authority integration transports start");
    proto::PlayerInfo info;info.modelPath="Assets/chr_fixture_postmodel.prefab";info.mapId="map01";info.levelId="level01";
    host.setLocalInfo(info);guest.setLocalInfo(info);
    proto::PlayerState pose;pose.flags=proto::StateValid;pose.mapHash=proto::fnv32("map01");
    auto scene=Json{{"map","map01"},{"level","level01"},{"level_num",1001},{"position",{0,0,0}},{"entities",Json::array()}};
    check(!h.publish(host,scene),"an uninitialized native scene cannot publish a world");
    h.setLocalSceneReady(true);g.setLocalSceneReady(true);
    HostOptions options;options.port=48841;host.host(options);
    check(wait([&]{return host.currentRole()==Role::Host;}),"authority host starts");
    guest.join("127.0.0.1:48841","");check(wait([&]{return guest.currentRole()==Role::Client;}),"authority guest joins");
    bool graphEnabled=false;
    auto tick=[&] {
        pose.timeMs=std::uint32_t(nowMs());host.setLocalState(pose);guest.setLocalState(pose);
        h.update(host);g.update(guest);h.publish(host,scene);
        if(h.ready()) {
            std::map<std::string,std::vector<Observation>> frame={
                {"player",{{"guest-character","player",{{"template","operator"},{"hp",100},{"max_hp",100}},3}}},
                {"mineral",{{"ore","mineral",{{"template","ore"},{"hp_ratio",ha.begins?9000:10000},{"max_hp_ratio",10000},{"collected",false}},4}}}};
            if(graphEnabled) {
                Observation cast{"native-cast","skill",{{"active",true}},5};cast.relations["caster"]={"player","guest-character",3,1};
                cast.relations["target"]={"mineral","ore",4,1};
                Observation projectile{"native-projectile","projectile",{{"active",true}},6};
                projectile.relations["parent"]={"skill","native-cast",5,1};
                frame["skill"]={cast};frame["projectile"]={projectile};
            }
            h.observeNativeFrame(frame);
            h.publishAuthority(host);
        }
    };
    const bool baselineReady=wait([&]{tick();return h.protocolCompatible(2)&&g.protocolCompatible(1)&&g.sharedObjects().size()==2;});
    if(!baselineReady)std::cerr<<"baseline fixture diagnostic host_ready="<<h.ready()<<" guest_ready="<<g.ready()<<" host_protocol="<<h.protocolCompatible(2)<<" guest_protocol="<<g.protocolCompatible(1)<<" host_objects="<<h.sharedObjects().size()<<" guest_objects="<<g.sharedObjects().size()<<" guest_error="<<g.status().at("error")<<std::endl;
    check(baselineReady,"native object baseline crosses authenticated room transport");
    Ref actor,target;
    const auto baseline=h.authorityBaseline();
    for(const auto& object:baseline.at("objects")) {
        auto ref=Ref::parse(object.at("ref"));if(object.at("kind")=="player")actor=ref;else target=ref;
    }
    check(!g.submitNativeIntent(guest,actor,target,"mine",1,1),"guest cannot submit before a host party grant");
    ha.certified=false;
    check(!h.grantNativeParty(host,2,9,{actor}),"unverified native adapter cannot open execution through transport");
    ha.certified=true;
    check(h.grantNativeParty(host,2,9,{actor}),"host grants only registered native party references");
    check(wait([&]{tick();return g.submitNativeIntent(guest,actor,target,"mine",1,1);}),"guest sends an intent only after native baseline application is observed");
    const bool resultReady=wait([&]{tick();return ha.begins==1&&h.status()["world_authority"]["host"]["results_confirmed"]==1&&g.sharedObjects()==h.sharedObjects();});
    if(!resultReady)std::cerr<<"result fixture diagnostic begins="<<ha.begins<<" cancels="<<ha.cancels<<" host="<<h.status().at("world_authority")<<" guest="<<g.status().at("world_authority")<<" rejected="<<h.status().at("rejected")<<std::endl;
    check(resultReady,"host native witness returns through unified result transport");
    check(ga.revision==g.status()["world_authority"]["replica_revision"],"shared guest applies the newer native version before permitting another intent");
    check(!g.submitNativeIntent(guest,target,target,"mine",2,1),"guest cannot substitute a non-party actor");
    const Scope scope{host.snapshot().roomId,baseline.at("scope").at("world").get<std::string>(),h.generation()};
    Json replay={{"kind","world_intent"},{"schema",coopWorldSchema},{"room",host.snapshot().roomId},
        {"intent",Intent{scope,2,9,1,1,1,1,actor,target,"mine"}.json()}};
    const auto rejected=h.status()["rejected"].get<std::uint64_t>();
    auto wire=replay.dump();guest.sendData(worldChannel,1,wire.data(),wire.size(),true);
    check(wait([&]{tick();return h.status()["rejected"].get<std::uint64_t>()>rejected;})&&ha.begins==1,"replayed intent cannot execute a second native action");
    replay["intent"]["peer"]=1;replay["intent"]["request"]=2;
    const auto spoofRejected=h.status()["rejected"].get<std::uint64_t>();wire=replay.dump();guest.sendData(worldChannel,1,wire.data(),wire.size(),true);
    check(wait([&]{tick();return h.status()["rejected"].get<std::uint64_t>()>spoofRejected;})&&ha.begins==1,"authenticated sender cannot claim another room member's identity");
    check(g.status()["world_authority"]["full_sync_verified"]==false,"transport fixture never claims real-game full synchronization");
    const Json controls={{"slot",2},{"phase","press"}};ha.acceptCast=true;
    check(!g.submitNativeIntent(guest,actor,target,"cast",2,1,{{"slot",2},{"phase","press"},{"damage",500}}),
        "guest input serialization rejects result fields before sending");
    check(g.submitNativeIntent(guest,actor,target,"cast",2,1,controls)&&
          wait([&]{tick();return ha.begins==2&&ha.lastInput==controls;}),"typed native controls cross the actual authenticated transport");
    graphEnabled=true;
    check(wait([&]{tick();return g.sharedObjects().size()==4&&g.sharedObjects()==h.sharedObjects();}),
        "caster, target and projectile parentage cross the same unified transport baseline and delta");
    g.setLocalSceneReady(false);g.update(guest);
    check(ga.saved&&ga.restoreStarted&&!g.submitNativeIntent(guest,actor,target,"mine",3,1),"loading closes intents and retains personal recovery until observed");
    bool detachRejected=false;try {g.attachNativeAdapter(nullptr);}catch(...) {detachRejected=true;}
    check(detachRejected,"adapter replacement cannot discard retained personal world");
    ga.restoreReady=true;g.update(guest);check(!ga.saved,"native restoration evidence releases the retained personal snapshot");
    g.attachNativeAdapter(nullptr);h.attachNativeAdapter(nullptr);guest.stop();host.stop();
    return 0;
}
int main() {try{return run();}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<std::endl;return 1;}}
