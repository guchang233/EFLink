// Real localhost room tests. Native execution/witness tests live separately;
// roster and display packets must never grant world authority.
#include "link_session.h"
#include <iostream>
using namespace eflink;
static void check(bool value,const char* name) {if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<std::endl;}
template<class F> static bool wait(F f,int milliseconds=4500) {for(int i=0;i<milliseconds/15;++i) {if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(15));}return f();}
static void raw(Net& net,Json body,std::uint16_t to=proto::kEveryone) {auto bytes=body.dump();net.sendData(worldChannel,to,bytes.data(),bytes.size(),true);}
static Json scene(const char* map="map01",const char* level="level01") {return {{"map",map},{"level",level},{"level_num",1001},{"position",{0,0,0}},{"entities",Json::array()}};}
int run() {
    Net host,guest,late;Session h,g,l;
    h.setAdapterVersion("fixture-game");g.setAdapterVersion("fixture-game");l.setAdapterVersion("fixture-game");
    h.setLocalSceneReady(true);g.setLocalSceneReady(true);l.setLocalSceneReady(true);
    host.onData=[&](auto peer,auto channel,auto data,auto count){if(channel==worldChannel)h.receive(peer,data,count);};
    guest.onData=[&](auto peer,auto channel,auto data,auto count){if(channel==worldChannel)g.receive(peer,data,count);};
    late.onData=[&](auto peer,auto channel,auto data,auto count){if(channel==worldChannel)l.receive(peer,data,count);};
    proto::PlayerInfo info;info.modelPath="Assets/chr_test_postmodel.prefab";info.mapId="map01";info.levelId="level01";
    host.setLocalInfo(info);guest.setLocalInfo(info);late.setLocalInfo(info);
    proto::PlayerState pose;pose.flags=proto::StateValid;pose.mapHash=proto::fnv32("map01");
    check(host.start(47901)&&guest.start(47902)&&late.start(47903),"room transports start");
    HostOptions options;options.port=47841;host.host(options);
    check(wait([&]{return host.currentRole()==Role::Host;}),"room host starts");
    guest.join("127.0.0.1:47841","");check(wait([&]{return guest.currentRole()==Role::Client;}),"guest joins transport");
    Json current=scene();bool censusEnabled=false;
    const Json nativeOre={{"template","ore"},{"position",{1,0,1}},{"hp_ratio",10000},{"max_hp_ratio",10000},{"collected",false}};
    auto tick=[&] {pose.timeMs=std::uint32_t(nowMs());host.setLocalState(pose);guest.setLocalState(pose);late.setLocalState(pose);
        h.update(host);g.update(guest);l.update(late);h.publish(host,current);
        if(censusEnabled&&h.ready()) {h.observeNativeObjects("mineral",{{"ore","mineral",nativeOre,55}});h.publishAuthority(host);}
    };
    check(wait([&]{tick();return h.ready()&&g.ready()&&h.protocolCompatible(2)&&g.protocolCompatible(1);}),"room manifests and protocol identity arrive");
    check(g.snapshot()["entities"].empty()&&g.snapshot()["players"].size()==2&&g.snapshot()["players"][0]["read_only"]==true&&
        !g.snapshot()["players"][0].contains("entity"),"room roster does not fabricate native actors or enemy outcomes");
    check(!g.canAct(2)&&!h.canAct(1)&&!h.canAct(2)&&!g.status()["world_authority"]["full_sync_verified"].get<bool>(),"scene and roster alone cannot admit gameplay");
    auto fabricated=current;fabricated["entities"].push_back({{"key","enemy"},{"hp",0}});
    check(!h.publish(host,fabricated,true),"scene manifests cannot carry a parallel enemy simulation");
    censusEnabled=true;
    check(h.observeNativeObjects("mineral",{{"ore","mineral",nativeOre,55}})&&h.publishAuthority(host,true)&&
        wait([&]{tick();return g.sharedObjects()==h.sharedObjects();}),"native object facts use the common ledger without gameplay admission");
    const auto before=h.sharedObjects();
    auto forged=Json{{"kind","authority_baseline"},{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"map","map01"},{"level","level01"},{"baseline",h.authorityBaseline()}};
    auto rejected=h.status()["rejected"].get<std::uint64_t>();raw(guest,forged,1);
    check(wait([&]{tick();return h.status()["rejected"].get<std::uint64_t>()>rejected;})&&h.sharedObjects()==before,"guest cannot submit world outcomes as a baseline");
    rejected=g.status()["rejected"].get<std::uint64_t>();forged["baseline"]["scope"]["epoch"]=h.generation()+1;raw(host,forged,2);
    check(wait([&]{tick();return g.status()["rejected"].get<std::uint64_t>()>rejected;})&&g.sharedObjects()==before,"another scene cannot overwrite current native objects");
    for(const char* kind:{"entry_prepare","entry_result","player_cast","skill_intent","hit_claim","hit","player_hit","enemy_state","enemy_cast","resource_baseline","character_growth","character_appearance","location","teleport_request","teleport","teleport_error"}) {
        const auto hr=h.status()["rejected"].get<std::uint64_t>(),gr=g.status()["rejected"].get<std::uint64_t>();
        Json packet={{"kind",kind},{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"active",true},{"amount",999},{"epoch",h.generation()}};
        raw(guest,packet,1);raw(host,packet,2);
        check(wait([&]{tick();return h.status()["rejected"].get<std::uint64_t>()>hr&&g.status()["rejected"].get<std::uint64_t>()>gr;})&&!g.canAct(2)&&!h.canAct(2),
            "retired parallel gameplay protocol is rejected in both directions");
    }
    bool teleportRejected=false;try {g.requestTeleport(guest,1);}catch(...) {teleportRejected=true;}
    check(teleportRejected,"V2 teleport cannot issue an unverified native travel command");
    Json member={{"slot",0},{"life",1ull},{"controlled",true},{"model",info.modelPath},{"flags",1},{"position",{0,0,0}},{"rotation",{0,0,0,1}},
        {"root",{0,0,0}},{"bones",Json::array()},{"names",Json::array()},{"growth",{{"template","chr_test"},{"level",50},{"break_stage",2},{"potential",0}}},
        {"appearance",{{"template","chr_test"},{"weapon_template",8002}}}};
    const Json display={{"sample_time",nowMs()},{"members",Json::array({member})},{"effects",Json::array()}};
    check(g.publishPresentation(guest,display)&&wait([&]{tick();return !h.presentation(2).is_null();})&&!h.canAct(2),"display metadata stays read-only and does not recreate actors");
    auto fakeScene=h.snapshot();fakeScene["players"][0]["native_bound"]=true;
    rejected=g.status()["rejected"].get<std::uint64_t>();
    raw(host,{{"kind","snapshot"},{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"epoch",h.generation()},{"revision",h.worldRevision()+1000},{"world",fakeScene}},2);
    check(wait([&]{tick();return g.status()["rejected"].get<std::uint64_t>()>rejected;})&&!g.canAct(2),"a manifest cannot assert a native binding or certification");
    const auto worldRevision=g.worldRevision();
    raw(host,{{"kind","snapshot_delta"},{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"epoch",h.generation()},{"revision",worldRevision+1000},
        {"base_revision",worldRevision+999},{"delta",Json::array()}},2);
    check(wait([&]{g.update(guest);return g.status()["transport"]["baseline_missing"].get<bool>();})&&!g.canAct(2),"a missing scene delta base immediately closes operation admission");
    h.publish(host,current,true);check(wait([&]{tick();return !g.status()["transport"]["baseline_missing"].get<bool>();}),"complete host manifest restores a missing scene baseline");
    late.join("127.0.0.1:47841","");check(wait([&]{return late.currentRole()==Role::Client;}),"late peer joins transport");
    check(wait([&]{tick();h.publishAuthority(host);return l.ready()&&l.sharedObjects()==h.sharedObjects()&&l.sharedObjects().size()==1;})&&!l.canAct(3),
        "late join receives the same native baseline and no fabricated admission");
    censusEnabled=false;const auto originalEpoch=h.generation();h.invalidateNativeScene();
    check(!h.ready()&&!h.publish(host,current,true)&&h.sharedObjects().empty(),"same-level native reload withdraws old object permissions immediately");
    h.setLocalSceneReady(true);check(h.publish(host,current,true)&&h.generation()==originalEpoch+1,"same names receive a new scene generation after native reload");
    check(wait([&]{tick();return g.generation()==h.generation()&&g.sharedObjects().empty();}),"old native baseline cannot survive the new scene generation");
    current=scene("map02","level02");h.publish(host,current,true);
    check(wait([&]{tick();return g.snapshot()["map"]=="map02"&&g.generation()==h.generation();}),"host map manifest changes revoke the old object scope");
    g.setLocalSceneReady(false);check(g.sharedObjects().empty()&&!g.canAct(2),"guest loading immediately withdraws shared operation access");
    g.setAdapterVersion("another-game");check(wait([&]{tick();return !h.protocolCompatible(2);})&&!g.canAct(2),"different game versions cannot negotiate cooperation");
    guest.leave();check(wait([&]{tick();return guest.currentRole()==Role::Offline&&!g.ready()&&g.sharedObjects().empty();}),"leaving clears room references without manufacturing personal-world results");
    host.stop();guest.stop();late.stop();return 0;
}
int main() {try{return run();}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<std::endl;return 1;}}
