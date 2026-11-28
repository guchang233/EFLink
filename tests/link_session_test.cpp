// End-to-end world protocol checks over real localhost sockets. No Unity substitutes.
#include "link_session.h"
#include "link_native_identity.h"
#include "link_combat_stats.h"
#include "link_operator_names.h"
#include "link_settings.h"
#include <cstdio>
using namespace eflink;
int failures=0;
void check(bool b,const char* name) {std::printf("%s %s\n",b?"[ok]":"[FAIL]",name);if(!b)++failures;}
template<class F> bool wait(F f,int ms=3000) {for(int i=0;i<ms/20;++i) {if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(20));}return f();}
void raw(Net& n,Json b,std::uint16_t to=proto::kEveryone) {auto text=b.dump();n.sendData(worldChannel,to,text.data(),text.size(),true);}
Json scene(const char* map="map01",const char* level="map01_lv001") {
    return {{"map",map},{"level",level},{"level_num",1001},{"position",{0,0,0}},
        {"entities",Json::array({{{"key","map01_lv001:level:100"},{"hp",100},{"max_hp",100},{"position",{1,0,1}},{"rotation",{0,0,0,1}}}})}};
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const std::string growthModel="Assets/chr_test_postmodel.prefab";
    const Json growth={{"template","chr_test"},{"level",50},{"break_stage",2},{"potential",0}};
    check(validCharacterGrowth(growth,growthModel),"public native character growth has a bounded model-matched format");
    auto invalidGrowth=growth;invalidGrowth["inst_id"]=123456;
    check(!validCharacterGrowth(invalidGrowth,growthModel),"account instance IDs cannot enter a public character profile");
    invalidGrowth=growth;invalidGrowth["hp"]=999999;
    check(!validCharacterGrowth(invalidGrowth,growthModel),"client-computed combat stats cannot enter character growth");
    invalidGrowth=growth;invalidGrowth["level"]=1.5;
    check(!validCharacterGrowth(invalidGrowth,growthModel),"fractional native character levels are rejected");
    invalidGrowth=growth;invalidGrowth["potential"]=-1;
    check(!validCharacterGrowth(invalidGrowth,growthModel)&&!validCharacterGrowth(growth,"Assets/chr_other_postmodel.prefab"),"invalid potential and another operator template are rejected");
    NativeIdentity allocation{17,3,0xEF00000000000021ull,true};
    check(allocation.matches(allocation),"the original native allocation retains its mutation lease");
    auto recycled=allocation;recycled.instance++;
    check(!allocation.matches(recycled),"a pooled entity reused at the same address loses the old lease");
    recycled=allocation;recycled.generation++;
    check(!allocation.matches(recycled),"another scene generation cannot retain an old entity lease");
    recycled=allocation;recycled.server++;
    check(!allocation.matches(recycled),"re-registration under another native server ID revokes the old lease");
    check(!allocation.matches({})&&!NativeIdentity{}.matches(allocation),"uninitialized identities never authorize cleanup");
    Net host,guest,late;Session h,g,l;
    host.onData=[&](auto from,auto channel,auto data,auto size){if(channel==worldChannel)h.receive(from,data,size);};
    guest.onData=[&](auto from,auto channel,auto data,auto size){if(channel==worldChannel)g.receive(from,data,size);};
    late.onData=[&](auto from,auto channel,auto data,auto size){if(channel==worldChannel)l.receive(from,data,size);};
    host.setName("host");guest.setName("guest");late.setName("late");
    proto::PlayerInfo info;info.modelPath=growthModel;info.mapId="map01";info.levelId="map01_lv001";host.setLocalInfo(info);guest.setLocalInfo(info);late.setLocalInfo(info);
    proto::PlayerState pose;pose.flags=proto::StateValid;pose.mapHash=proto::fnv32("map01");host.setLocalState(pose);guest.setLocalState(pose);late.setLocalState(pose);
    check(host.start(47901)&&guest.start(47902)&&late.start(47903),"three transports start");
    HostOptions options;options.port=47841;host.host(options);check(wait([&]{return host.currentRole()==Role::Host;}),"host room starts");
    guest.join("127.0.0.1:47841","");check(wait([&]{return guest.currentRole()==Role::Client&&host.snapshot().peers.size()==2;}),"guest joins");
    Realm::NativeState nativeFixture{{0,0,0},100,100,true};
    auto tick=[&]{pose.timeMs=std::uint32_t(nowMs());host.setLocalState(pose);guest.setLocalState(pose);late.setLocalState(pose);h.update(host);g.update(guest);l.update(late);
        h.observePlayer(1,h.playerEntity(1),h.generation(),nativeFixture);h.observePlayer(2,h.playerEntity(2),h.generation(),nativeFixture);};tick();
    check(h.publish(host,scene(),true),"host publishes a real world snapshot");
    check(wait([&]{tick();return g.ready();}),"guest receives host map and enemies");
    g.publishGrowth(guest,growthModel,growth);
    check(wait([&]{tick();return h.requestedGrowth(2)==growth;}),"the host receives public growth before admitting a guest");
    check(!h.recordGrowth(2,h.playerEntity(2),h.generation(),growth),"a growth request alone is not a confirmed native profile");
    check(g.snapshot().at("entities")[0].at("hp")==100,"late state contains authoritative HP");
    check(!g.canAct(2)&&!g.submitSkill(guest,"map01_lv001:level:100","normal"),"snapshot alone cannot admit a player or settle combat");
    ReplicaFacts complete{true,true,true,true};
    check(wait([&]{tick();g.reportEntry(guest,complete);return !g.status()["entry"]["blockers"].empty();})&&
        !g.canAct(2),"host without an authority adapter cannot admit a fully prepared guest");
    // A protocol-only fixture explicitly supplies capabilities. The game adapter
    // must prove them through native implementation; production currently cannot.
    h.setAuthority({true,true,true,true});
    tick();g.reportEntry(guest,complete);
    check(!h.canAct(2),"authority flags cannot substitute for a bound native player actor");
    const auto guestEntity=h.playerEntity(2);
    check(guestEntity&&h.playerEntity(1)!=guestEntity&&g.playerEntity(2)==guestEntity,"host assigns distinct entity IDs and guest uses the host's ID");
    const std::map<std::string,Realm::SkillRule> skills{{"normal",{1000,5}}};
    check(h.bindPlayer(1,growthModel,skills)&&h.bindPlayer(2,growthModel,skills),"protocol fixture binds host and guest actors with a host-owned skill catalog");
    check(!h.bindPlayer(2,growthModel,skills),"repeated actor binding cannot reset cooldown state");
    check(!h.recordGrowth(2,h.playerEntity(1),h.generation(),growth)&&h.recordGrowth(2,h.playerEntity(2),h.generation(),growth),"only the bound native identity confirms a player's reconstructed growth");
    check(!g.observePlayer(2,guestEntity,g.generation(),nativeFixture),"a guest adapter cannot supply authoritative player HP/position");
    check(!h.observePlayer(2,h.playerEntity(1),h.generation(),nativeFixture),"native observations cannot be attached to another player's identity");
    auto invalidState=nativeFixture;invalidState.hp=-1;
    check(!h.observePlayer(2,guestEntity,h.generation(),invalidState),"invalid native player HP is rejected");
    tick();
    h.publish(host,scene(),true);
    check(wait([&]{tick();g.reportEntry(guest,complete);return g.canAct(2)&&h.canAct(2);}),"prepared guest becomes active only after host commit");
    check(g.requestedGrowth(2)==growth,"guests use the host-confirmed profile rather than a peer's claim");
    invalidGrowth=growth;invalidGrowth["level"]=51;
    auto growthRejected=h.status()["rejected"].get<int>();
    raw(guest,{{"schema",2},{"room",host.snapshot().roomId},{"kind","character_growth"},{"model",growthModel},{"growth",invalidGrowth}},1);
    check(wait([&]{tick();return h.status()["rejected"].get<int>()>growthRejected;})&&h.requestedGrowth(2)==growth,"an active player cannot change growth to recreate its actor and reset cooldowns");
    auto r=g.status().at("revision");auto bad=scene();bad["entities"][0]["hp"]=-1;
    raw(host,{{"schema",2},{"room",host.snapshot().roomId},{"kind","snapshot"},{"epoch",1},{"revision",99},{"world",bad}});
    check(wait([&]{tick();return g.status()["rejected"].get<int>()>0;})&&g.status()["revision"]==r,"negative HP cannot replace the world");
    bad=scene();bad["entities"].push_back(bad["entities"][0]);check(!validWorld(bad),"duplicate entity identifiers are rejected");
    bad=scene();bad["entities"][0]["rotation"]={0,0,0,0};check(!validWorld(bad),"invalid rotation rejected");
    late.join("127.0.0.1:47841","");check(wait([&]{return late.currentRole()==Role::Client&&host.snapshot().peers.size()==3;}),"late peer joins");
    check(wait([&]{tick();h.publish(host,scene(),true);return l.ready();}),"late peer gets complete current world");
    check(h.nativeState(1).has_value(),"a late join does not erase the host's native observation or binding");
    check(!l.canAct(3)&&!h.canAct(3),"late joiner must complete its own entry handshake");
    ReplicaFacts partial{true,true,false,false};
    check(wait([&]{tick();l.reportEntry(late,partial);return !l.status()["entry"]["blockers"].empty();})&&!l.canAct(3),
        "matched native entities do not prove guest-world isolation");
    raw(guest,{{"schema",2},{"room",host.snapshot().roomId},{"kind","snapshot"},{"epoch",90},{"revision",1},{"world",scene()}},proto::kEveryone);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));tick();
    check(l.generation()==1&&h.generation()==1,"guest cannot impersonate the world authority");
    check(wait([&]{tick();return host.snapshot().peers.size()==3&&guest.snapshot().peers.size()==3&&host.snapshot().peers[1].hasInfo&&!host.snapshot().peers[1].states.empty();}),"teleport targets have native player metadata");
    nativeFixture.position={3,0,7};tick();
    g.requestTeleport(guest,1);
    Json teleport;check(wait([&]{tick();teleport=g.takeTeleport();return !teleport.empty();})&&teleport["map"]=="map01"&&teleport["level_num"]==1001,"host authorizes same/cross-map travel with map data");
    check(teleport.value("position",Json::array())==Json::array({3.,0.,7.}),"teleport target comes from host native state rather than UDP pose");
    auto grants=h.takeActorTeleports();
    check(grants.contains(2)&&grants.at(2).at("position")==teleport.at("position"),"guest teleport also authorizes its host-owned native actor");
    check(h.takeActorTeleports().empty(),"native actor teleport grants are consumed once");
    nativeFixture.position={0,0,0};tick();
    g.submitSkill(guest,"map01_lv001:level:100","normal");
    std::deque<Json> commands;check(wait([&]{tick();commands=h.takeCommands();return !commands.empty();})&&
        commands.front()["actor"]==guestEntity&&!commands.front().contains("amount"),"guest skill intent uses its own actor and carries no damage result");
    if(commands.empty()) {host.stop();guest.stop();late.stop();return 1;}
    auto accepted=h.status()["skill_intents_accepted"].get<int>();auto seq=commands.front()["seq"];
    raw(guest,{{"schema",2},{"room",host.snapshot().roomId},{"kind","skill_intent"},{"epoch",1},{"seq",seq},{"actor",guestEntity},{"key","map01_lv001:level:100"},{"skill","normal"}} ,1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));tick();
    check(h.takeCommands().empty()&&h.status()["skill_intents_accepted"]==accepted,"replayed skill cannot apply twice");
    auto before=h.status()["rejected"].get<int>();
    raw(guest,{{"schema",2},{"room",host.snapshot().roomId},{"kind","skill_intent"},{"epoch",1},{"seq",100},{"actor",guestEntity},{"key","missing"},{"skill","normal"}},1);
    check(wait([&]{tick();return h.status()["rejected"].get<int>()>before;})&&h.takeCommands().empty(),"unknown target is rejected");
    std::uint64_t rejectedSequence=100;
    auto rejectIntent=[&](Json extra,const char* name) {
        auto prior=h.status()["rejected"].get<int>();
        Json body={{"schema",2},{"room",host.snapshot().roomId},{"kind","skill_intent"},{"epoch",1},{"seq",++rejectedSequence},
            {"actor",guestEntity},{"key","map01_lv001:level:100"},{"skill","normal"}};
        body.update(extra);raw(guest,body,1);
        check(wait([&]{tick();return h.status()["rejected"].get<int>()>prior;})&&h.takeCommands().empty(),name);
    };
    rejectIntent({{"actor",h.playerEntity(1)}},"guest cannot cast as the host's actor");
    rejectIntent({{"amount",999999999}},"client-computed damage is rejected");
    rejectIntent({{"skill","unowned_skill"}},"guest cannot invent a skill absent from the host's catalog");
    rejectIntent(Json::object(),"host enforces its skill cooldown");
    // Player-resolved hits: the attacker's client reports a bounded result (with optional crit & damage type).
    check(g.submitHit(guest,"map01_lv001:level:100",12.5,true,2),"an admitted guest reports a resolved hit with crit & element metadata");
    std::deque<Json> hits;check(wait([&]{tick();hits=h.takeHits();return !hits.empty();})&&hits.front()["from"]==2&&
        hits.front()["amount"]==12.5&&hits.front()["key"]=="map01_lv001:level:100"&&
        hits.front().value("crit",false)==true&&hits.front().value("dtype",0)==2,"host receives the guest's hit and crit/element metadata for its own enemy");
    std::uint64_t hitSeq=1000;
    auto rejectHit=[&](Json extra,const char* name) {
        auto prior=h.status()["rejected"].get<int>();
        Json body={{"schema",2},{"room",host.snapshot().roomId},{"kind","hit"},{"epoch",1},{"seq",++hitSeq},{"key","map01_lv001:level:100"},{"amount",5}};
        body.update(extra);raw(guest,body,1);
        check(wait([&]{tick();return h.status()["rejected"].get<int>()>prior;})&&h.takeHits().empty(),name);
    };
    rejectHit({{"amount",101}},"one hit cannot exceed the enemy's whole health");
    rejectHit({{"amount",-3}},"negative hits are rejected");
    rejectHit({{"key","missing"}},"hits on unknown enemies are rejected");
    rejectHit({{"epoch",7}},"hits from another world epoch are rejected");
    rejectHit({{"seq",1}},"replayed hits are rejected");
    check(h.sendPlayerHit(host,2,30,"map01_lv001:level:100"),"host forwards an enemy hit on a guest's actor");
    std::deque<Json> received;check(wait([&]{tick();received=g.takePlayerHits();return !received.empty();})&&received.front()["amount"]==30,
        "the guest receives the enemy hit for its own operator");
    check(g.sendPlayerHit(guest,1,25,"map01_lv001:level:100"),"a guest forwards an enemy hit on the host's stand-in");
    check(wait([&]{tick();received=h.takePlayerHits();return !received.empty();})&&received.front()["amount"]==25,
        "the host receives the hit for its own operator");
    check(!g.sendPlayerHit(guest,2,25,"x"),"a guest cannot address a hit to itself");
    check(h.sendEnemyCast(host,"map01_lv001:level:100","enemy_attack1",2),"host announces a room enemy's native cast");
    std::deque<Json> casts;check(wait([&]{tick();casts=g.takeEnemyCasts();return !casts.empty();})&&casts.front()["skill"]=="enemy_attack1"&&casts.front()["target"]==2,
        "guests receive the enemy cast and its target player");
    check(!g.sendEnemyCast(guest,"map01_lv001:level:100","enemy_attack1",1),"only the host announces enemy casts");
    const Json moved=Json::array({{{"key","map01_lv001:level:100"},{"position",{2,0,3}},{"rotation",{0,0,0,1}},{"engaged",true}}});
    check(g.sendEnemyStates(guest,moved),"an admitted guest reports enemies its client simulates");
    std::deque<Json> reports;check(wait([&]{tick();reports=h.takeEnemyStates();return !reports.empty();})&&reports.front()["from"]==2&&
        reports.front()["states"][0]["position"]==Json::array({2,0,3}),"host receives the guest's enemy movement report");
    auto stateRejected=h.status()["rejected"].get<int>();
    raw(guest,{{"schema",2},{"room",host.snapshot().roomId},{"kind","enemy_state"},{"epoch",1},
        {"states",Json::array({{{"key","map01_lv001:level:100"},{"position",{1e9,0,0}},{"rotation",{0,0,0,1}}}})}},1);
    check(wait([&]{tick();return h.status()["rejected"].get<int>()>stateRejected;})&&h.takeEnemyStates().empty(),"out-of-range enemy positions are rejected");
    check(g.reportEnemyCast(guest,"map01_lv001:level:100","enemy_attack1",2),"a guest reports a cast by an enemy it simulates");
    std::deque<Json> guestCasts;check(wait([&]{tick();guestCasts=h.takeEnemyCasts();return !guestCasts.empty();})&&guestCasts.front()["from"]==2,
        "host receives the guest-simulated enemy cast with its sender");
    auto lastAccepted=nowMs();
    nativeFixture.position[0]=50;pose.position[0]=0;guest.setLocalState(pose);
    check(wait([&]{tick();return nowMs()-lastAccepted>=1100;}),"cooldown expires before testing native range validation");
    rejectIntent(Json::object(),"forged near UDP pose cannot bypass the host's native skill range");
    pose.position[0]=50;guest.setLocalState(pose);
    check(wait([&]{tick();for(const auto& peer:host.snapshot().peers)if(peer.id==2&&!peer.states.empty())return peer.states.back().state.position[0]==50;return false;}),
        "host observes the attacker's updated position");
    rejectIntent(Json::object(),"skill range comes from the host's catalog");
    pose.position[0]=0;nativeFixture.position[0]=0;
    h.publish(host,scene("map02","map02_lv001"),true);
    check(wait([&]{tick();return g.generation()==2&&l.generation()==2;}),"host map changes create a new world epoch for every guest");
    check(!g.canAct(2)&&!h.canAct(2),"map change revokes entry until the new world is prepared");
    check(h.playerEntity(2)!=guestEntity&&!h.canAct(1),"scene change replaces actor IDs and removes every native actor binding");
    raw(host,{{"schema",2},{"room",host.snapshot().roomId},{"kind","entry_result"},{"epoch",1},{"token",1},{"active",true},{"blockers",Json::array()}},2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));tick();
    check(!g.canAct(2),"stale entry grant cannot admit a player to a different map");
    info.mapId="map02";info.levelId="map02_lv001";pose.mapHash=proto::fnv32("map02");
    host.setLocalInfo(info);guest.setLocalInfo(info);
    check(wait([&]{tick();return h.bindPlayer(1,growthModel,skills);})&&
        wait([&]{tick();return h.bindPlayer(2,growthModel,skills);}),"new scene requires new native actor bindings");
    h.publish(host,scene("map02","map02_lv001"),true);
    check(wait([&]{tick();g.reportEntry(guest,complete);return g.canAct(2);}),"new scene requires a fresh entry commit");
    g.reportEntry(guest,partial);check(!g.canAct(2),"losing local isolation immediately suspends guest actions");
    check(wait([&]{tick();g.reportEntry(guest,complete);return g.canAct(2);}),"restored replica requires another authority acknowledgement");
    h.unbindPlayer(2);check(!h.canAct(2),"losing the native guest actor immediately revokes its entry");
    h.setAuthority({});check(!h.canAct(2)&&!h.canAct(1),"withdrawn host capabilities immediately stop authoritative settlement");
    raw(host,{{"schema",2},{"room",host.snapshot().roomId},{"kind","snapshot"},{"epoch",1},{"revision",999},{"world",scene()}});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));tick();
    check(g.snapshot()["map"]=="map02"&&g.generation()==2,"old-map snapshots cannot corrupt a new map");
    // PVP duels: an invitation must be accepted; the host keeps the pairs.
    h.pvp=false;g.duelAction(guest,"invite",1);
    check(wait([&]{tick();return g.duelNotice=="房主没有允许 PVP";})&&g.duels.empty(),"no duel invitations while the room does not allow PVP");
    h.pvp=true;g.duelAction(guest,"invite",1);
    check(wait([&]{tick();return h.duelInvites.contains(2);}),"the host receives the guest's invitation");
    check(h.duels.empty()&&g.duels.empty()&&!h.duelAllowed(2,1),"an invitation alone allows no PVP");
    h.duelAction(host,"decline",2);
    check(wait([&]{tick();return g.duelNotice.find("拒绝")!=std::string::npos;})&&!h.duelAllowed(2,1),"a declined invitation is reported and allows no PVP");
    g.duelAction(guest,"invite",1);check(wait([&]{tick();return h.duelInvites.contains(2);}),"a new invitation can follow a refusal");
    h.duelAction(host,"accept",2);
    check(wait([&]{tick();return h.duels.contains(2)&&g.duels.contains(1);})&&h.duelAllowed(2,1)&&h.duelAllowed(1,2),"an accepted invitation starts the duel on both sides");
    g.duelAction(guest,"end",1);
    check(wait([&]{tick();return h.duels.empty()&&g.duels.empty();})&&!h.duelAllowed(2,1),"either player can end the duel");
    g.duelAction(guest,"invite",1);check(wait([&]{tick();return h.duelInvites.contains(2);}),"invite again");
    h.duelAction(host,"accept",2);check(wait([&]{tick();return g.duels.contains(1);}),"duel again");
    h.pvp=false;check(wait([&]{tick();return h.duels.empty()&&g.duels.empty();}),"turning PVP off ends every duel");

    // Keep host world snapshot fresh for extended vitals / shared ATB / combat stats checks.
    auto tickFresh=[&]{tick();h.publish(host,scene("map02","map02_lv001"),false);};
    h.setAuthority({true,true,true,true});
    h.bindPlayer(1,growthModel,skills);h.bindPlayer(2,growthModel,skills);
    h.publish(host,scene("map02","map02_lv001"),true);
    check(wait([&]{tickFresh();g.reportEntry(guest,complete);return g.canAct(2);}),"re-admit guest for extended combat protocol checks");

    // Extended vitals (HP + ultimate SP + operator name + alive)
    check(g.sendVitals(guest,8200,10000,75.5,100.0,"伊冯",true),"guest broadcasts extended vitals");
    std::deque<Json> vitalsIn;
    check(wait([&]{tickFresh();vitalsIn=h.takeVitals();return !vitalsIn.empty();})&&
        vitalsIn.front().value("usp",0.0)==75.5&&vitalsIn.front().value("max_usp",0.0)==100.0&&
        vitalsIn.front().value("op",std::string{})=="伊冯"&&vitalsIn.front().value("alive",false)==true,"host receives extended teammate vitals");

    // Shared ATB pool: host-authoritative gating, delta validation, replay rejection, state broadcast
    h.sharedAtb=false;
    auto atbRejBefore=h.status()["rejected"].get<int>();
    check(g.sendAtbDelta(guest,-100.f,"cost",false),"guest sends atb delta while disabled");
    check(wait([&]{tickFresh();return h.status()["rejected"].get<int>()>atbRejBefore;})&&h.takeAtbDeltas().empty(),"host rejects atb_delta when shared ATB is disabled");
    h.sharedAtb=true;
    check(g.sendAtbDelta(guest,-100.f,"cost",false),"guest sends valid atb cost delta when shared ATB is enabled");
    std::deque<Json> deltas;
    check(wait([&]{tickFresh();deltas=h.takeAtbDeltas();return !deltas.empty();})&&
        deltas.front().value("from",0)==2&&deltas.front().value("delta",0.0)==-100.0,"host accepts valid guest atb_delta");
    auto atbRejOob=h.status()["rejected"].get<int>();
    raw(guest,{{"schema",2},{"room",host.snapshot().roomId},{"kind","atb_delta"},{"epoch",h.generation()},{"seq",999},{"delta",-9999.0},{"reason","cost"},{"pause",false}},1);
    check(wait([&]{tickFresh();return h.status()["rejected"].get<int>()>atbRejOob;})&&h.takeAtbDeltas().empty(),"host rejects out-of-bounds atb_delta");
    auto atbRejReplay=h.status()["rejected"].get<int>();
    raw(guest,{{"schema",2},{"room",host.snapshot().roomId},{"kind","atb_delta"},{"epoch",h.generation()},{"seq",1},{"delta",-50.0},{"reason","cost"},{"pause",false}},1);
    check(wait([&]{tickFresh();return h.status()["rejected"].get<int>()>atbRejReplay;})&&h.takeAtbDeltas().empty(),"host rejects replayed atb_delta seq");
    check(h.sendAtbState(host,true,185.5f,300.f,8.0f,2,"guest",-100.f,"cost"),"host broadcasts authoritative shared ATB state");
    std::deque<Json> atbStates;
    check(wait([&]{tickFresh();atbStates=g.takeAtbUpdates();return !atbStates.empty();})&&
        atbStates.front().value("enabled",false)==true&&std::abs(atbStates.front().value("value",0.0)-185.5)<0.1&&
        atbStates.front().value("actor",0)==2,"guest receives authoritative shared ATB state and actor attribution");
    check(!g.sendAtbState(guest,true,300.f,300.f,8.f),"guest cannot broadcast authoritative atb_state");

    // CombatStats engine + host->guest sync + guest->host reset
    CombatStats stats;
    stats.autoReset=false;
    stats.recordHit(1,"房主","管理员",1000.0,true,2,false,10000);
    stats.recordHit(1,"房主","管理员",500.0,false,0,true,12000);
    stats.recordHit(2,"队友A","伊冯",3000.0,true,4,false,12000);
    stats.recordTaken(2,"队友A","伊冯",450.0,12000);
    auto snap=stats.snapshot(14000);
    check(snap.value("total_damage",0.0)==4500.0&&snap["players"].size()==2&&snap["players"][0]["id"]==2,"CombatStats ranks players by total damage");
    check(std::abs(snap["players"][0]["share"].get<double>()-(3000.0/4500.0))<0.01&&snap["players"][0]["crit_rate"].get<double>()==1.0,"CombatStats computes share and crit rate");
    check(snap["players"][1]["kills"].get<std::uint64_t>()==1&&snap["players"][1]["hits"].get<std::uint64_t>()==2,"CombatStats tracks hits and kills");
    check(snap["players"][0]["taken"].get<double>()==450.0,"CombatStats tracks damage taken");
    auto snapFrozen=stats.snapshot(25000); // > 6s after last hit (12000): duration freezes at 2s (12000-10000)
    check(std::abs(snapFrozen.value("duration_s",0.0)-2.0)<0.05,"CombatStats freezes fight duration after 6s out of combat");
    check(CombatStats::validRemoteSnapshot(snap),"valid CombatStats snapshot passes schema check");
    auto badSnap=snap;badSnap["total_damage"]=-10.0;
    check(!CombatStats::validRemoteSnapshot(badSnap),"negative total_damage snapshot is rejected");
    check(h.sendDamageStats(host,snap),"host broadcasts combat damage stats snapshot");
    std::optional<Json> remoteStats;
    check(wait([&]{tick();remoteStats=g.takeRemoteDamageStats();return remoteStats.has_value();})&&
        remoteStats->value("total_damage",0.0)==4500.0,"guest receives validated combat stats snapshot");
    // Nested PvP snapshot validation + broadcast
    CombatStats pvpStats;
    pvpStats.recordHit(2,"队友A","伊冯",1800.0,true,4,true,15000);
    pvpStats.recordTaken(1,"房主","管理员",1800.0,15000);
    auto combinedSnap=snap;combinedSnap["pvp"]=pvpStats.snapshot(16000);
    check(CombatStats::validRemoteSnapshot(combinedSnap),"valid combined PvE+PvP snapshot passes schema check");
    auto badNested=combinedSnap;badNested["pvp"]["total_damage"]=-5.0;
    check(!CombatStats::validRemoteSnapshot(badNested),"negative nested PvP damage snapshot is rejected");
    check(h.sendDamageStats(host,combinedSnap),"host broadcasts combined PvE+PvP stats snapshot");
    check(wait([&]{tick();remoteStats=g.takeRemoteDamageStats();return remoteStats.has_value();})&&
        remoteStats->contains("pvp")&&remoteStats->at("pvp").value("total_damage",0.0)==1800.0,"guest receives nested PvP combat stats snapshot");
    // Guest PvE damage report & PVP damage report (duel-gated)
    check(g.sendDamageReport(guest,"pve",2,0,2200.0,true,4,false,false,"伊冯"),"guest submits local PvE damage report");
    std::deque<Json> dmgReports;
    check(wait([&]{tickFresh();dmgReports=h.takeDamageReports();return !dmgReports.empty();})&&
        dmgReports.front().value("category",std::string{})=="pve"&&dmgReports.front().value("attacker",0)==2&&
        dmgReports.front().value("amount",0.0)==2200.0&&dmgReports.front().value("crit",false)==true,"host receives validated guest PvE damage report");
    auto pvpReportRej=h.status()["rejected"].get<int>();
    check(g.sendDamageReport(guest,"pvp",1,2,900.0,false,0,false,false,"伊冯"),"guest sends PvP report while no duel is active");
    check(wait([&]{tickFresh();return h.status()["rejected"].get<int>()>pvpReportRej;})&&h.takeDamageReports().empty(),"host rejects PvP damage report outside active duel");
    h.pvp=true;g.duelAction(guest,"invite",1);
    check(wait([&]{tickFresh();return h.duelInvites.contains(2);}),"invite host for PvP stats test");
    h.duelAction(host,"accept",2);
    check(wait([&]{tickFresh();return h.duelAllowed(1,2)&&g.duels.contains(1);}),"accept PvP duel for stats test");
    check(h.sendPlayerHit(host,2,900.0,"pvp:1",true,2),"host sends PvP player_hit with crit & element metadata");
    std::deque<Json> pvpIn;
    check(wait([&]{tickFresh();pvpIn=g.takePlayerHits();return !pvpIn.empty();})&&
        pvpIn.front().value("crit",false)==true&&pvpIn.front().value("dtype",0)==2,"guest receives PvP hit crit & element metadata");
    check(g.sendDamageReport(guest,"pvp",1,2,900.0,true,2,true,false,"伊冯"),"guest reports settled PvP hit taken during active duel");
    check(wait([&]{tickFresh();dmgReports=h.takeDamageReports();return !dmgReports.empty();})&&
        dmgReports.front().value("category",std::string{})=="pvp"&&dmgReports.front().value("attacker",0)==1&&
        dmgReports.front().value("target",0)==2&&dmgReports.front().value("killed",false)==true,"host receives settled PvP damage report");
    h.pvp=false;
    check(g.requestDamageReset(guest,"pvp"),"guest requests scoped PvP combat stats reset");
    check(wait([&]{tick();return h.takeDamageResetScope()=="pvp";}),"host receives guest scoped PvP combat stats reset request");
    stats.autoReset=true;
    stats.recordHit(1,"房主","管理员",200.0,false,0,false,40000); // >15s after 12000 -> auto resets
    check(stats.snapshot(41000).value("total_damage",0.0)==200.0,"CombatStats auto-resets after 15s idle when enabled");
    check(operatorDisplayName("Assets/chr_0017_yvonne_postmodel.prefab")=="伊冯"&&!formatDamageNumber(125400.0).empty(),"operator name resolver and damage formatter work");

    // Settings validation for new UI/HUD/ATB/auto-follow keys
    bool validOk=true;
    try {validateSettings({{"auto_follow_host_map",true},{"room_shared_atb",true},{"show_damage_hud",true},{"show_team_hud",true},{"show_atb_hud",true},{"hud_compact",true},{"hud_opacity",0.86},{"hud_style","telemetry"},{"damage_auto_reset",true},{"damage_reset_key",VK_F9}});}
    catch(...) {validOk=false;}
    for(auto s:{"telemetry","chamfer","hazard","material"}) {
        try {validateSettings({{"hud_style",s}});} catch(...) {validOk=false;}
    }
    check(validOk,"validateSettings accepts valid new HUD, auto-follow, hud_style, and shared ATB settings");
    bool badAutoFollow=false;
    try {validateSettings({{"auto_follow_host_map","yes"}});} catch(...) {badAutoFollow=true;}
    bool badOpacity=false;
    try {validateSettings({{"hud_opacity",0.10}});} catch(...) {badOpacity=true;}
    bool badHudStyle=false;
    try {validateSettings({{"hud_style","neon_rainbow"}});} catch(...) {badHudStyle=true;}
    bool badResetKey=false;
    try {validateSettings({{"damage_reset_key",13}});} catch(...) {badResetKey=true;}
    check(badAutoFollow&&badOpacity&&badHudStyle&&badResetKey,"validateSettings rejects invalid auto_follow_host_map, hud_opacity, hud_style, and damage_reset_key");

    // Fast atomic ID/room getters match snapshot
    check(host.localIdFast()==host.snapshot().localId&&host.roomIdFast()==host.snapshot().roomId&&
          guest.localIdFast()==guest.snapshot().localId&&guest.roomIdFast()==guest.snapshot().roomId,
          "fast atomic localId/roomId match network snapshot");

    // Teleport error feedback when target is invalid/stale, and fallback to settled host world snapshot
    // when host's PlayerInfo mapId momentarily lags behind world["map"].
    g.requestTeleport(guest,99);
    check(wait([&]{tickFresh();return g.status().value("error",std::string{}).find("尚未进入")!=std::string::npos;}),
          "routeTeleport returns teleport_error to guest when target peer is unavailable");
    proto::PlayerInfo staleHostInfo=info;staleHostInfo.mapId="map01";staleHostInfo.levelId="map01_lv001";
    host.setLocalInfo(staleHostInfo);
    check(wait([&]{tickFresh();return host.snapshot().peers[0].info.mapId=="map01";}),"simulate transiently stale host PlayerInfo mapId");
    Json dungeonScene=scene("map02","map02_lv001");dungeonScene["dungeon"]="dung_test_01";dungeonScene["position"]={12.0,1.5,34.0};
    h.publish(host,dungeonScene,true);
    check(wait([&]{tick();return g.snapshot().value("dungeon",std::string{})=="dung_test_01";}),"host publishes settled dungeon world");
    g.requestTeleport(guest,1);
    Json hostTp;
    check(wait([&]{tick();hostTp=g.takeTeleport();return !hostTp.empty();})&&
          hostTp.value("map",std::string{})=="map02"&&hostTp.value("dungeon",std::string{})=="dung_test_01",
          "teleport to host succeeds using settled world snapshot even when host PlayerInfo lags");

    guest.leave();check(wait([&]{tick();return guest.currentRole()==Role::Offline&&!g.ready()&&!g.canAct(2);}),"leave clears guest world and readiness");
    host.stop();guest.stop();late.stop();
    return failures?1:0;
}
