// End-to-end world protocol checks over real localhost sockets. No Unity substitutes.
#include "link_session.h"
#include "link_native_identity.h"
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
    const Json appearance={{"template","chr_test"},{"weapon_template",8002}};
    Json advertisedAppearance=nullptr;
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
    h.setAdapterVersion("fixture-game");g.setAdapterVersion("fixture-game");l.setAdapterVersion("fixture-game");
    h.setLocalSceneReady(true);g.setLocalSceneReady(true);l.setLocalSceneReady(true);
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
        g.publishAppearance(guest,growthModel,advertisedAppearance);
        h.observePlayer(1,h.playerEntity(1),h.generation(),nativeFixture);h.observePlayer(2,h.playerEntity(2),h.generation(),nativeFixture);};tick();
    check(h.publish(host,scene(),true),"host publishes a real world snapshot");
    check(wait([&]{tick();return g.ready();}),"guest receives host map and enemies");
    check(wait([&]{tick();return h.protocolCompatible(2)&&g.protocolCompatible(1);}),"both game adapters negotiate the current cooperation contract");
    const Json nativeOre={{"template","mineral_test"},{"position",{1,0,1}},{"hp_ratio",10000},{"max_hp_ratio",10000},{"collected",false}};
    check(h.observeNativeObjects("mineral",{{"ore-12","mineral",nativeOre,55}})&&h.publishAuthority(host,true)&&
        wait([&]{tick();return g.sharedObjects()==h.sharedObjects();}),"host native minerals use unified authority baseline without granting gameplay admission");
    const auto resource=h.sharedObjects();
    auto forgedResource=Json{{"kind","authority_baseline"},{"schema",coopWorldSchema},{"room",host.snapshot().roomId},
        {"map","map01"},{"level","map01_lv001"},{"baseline",h.authorityBaseline()}};
    auto resourceRejected=h.status()["rejected"].get<unsigned>();raw(guest,forgedResource,1);
    check(wait([&]{tick();return h.status()["rejected"].get<unsigned>()>resourceRejected;})&&h.sharedObjects()==resource,
        "guest cannot publish mineral durability or destruction as authority");
    resourceRejected=g.status()["rejected"].get<unsigned>();forgedResource["baseline"]["scope"]["epoch"]=h.generation()+1;raw(host,forgedResource,2);
    check(wait([&]{tick();return g.status()["rejected"].get<unsigned>()>resourceRejected;})&&g.sharedObjects()==resource,
        "resource observations from another scene generation cannot replace the active baseline");
    resourceRejected=g.status()["rejected"].get<unsigned>();forgedResource["baseline"]["scope"]["epoch"]=h.generation();forgedResource["baseline"]["revision"]=0ull;raw(host,forgedResource,2);
    check(wait([&]{tick();return g.status()["rejected"].get<unsigned>()>resourceRejected;}),"old authority baseline cannot roll back committed objects");
    g.setLocalSceneReady(false);check(g.sharedObjects().empty(),"loading revokes all shared object references immediately");g.setLocalSceneReady(true);
    Json member={{"slot",0},{"life",1ull},{"controlled",true},{"model",growthModel},{"flags",1},
        {"position",{0,0,0}},{"rotation",{0,0,0,1}},{"root",{0,0,0}},{"bones",Json::array()},{"names",Json::array()},
        {"growth",growth},{"appearance",appearance}};
    Json presentation={{"sample_time",nowMs()},{"members",Json::array({member})},{"effects",Json::array()}};
    check(g.publishPresentation(guest,presentation)&&wait([&]{tick();return !h.presentation(2).is_null();}),
        "guest presentation arrives without granting native damage admission");
    auto stalePresentation=h.presentation(2);stalePresentation.erase("received");stalePresentation["seq"]=0ull;
    auto beforePresentationReject=h.status()["rejected"].get<unsigned>();raw(guest,stalePresentation,1);
    check(wait([&]{tick();return h.status()["rejected"].get<unsigned>()>beforePresentationReject;}),"zero and duplicate presentation sequences cannot replay a frame");
    auto wrongEpoch=stalePresentation;wrongEpoch["seq"]=100ull;wrongEpoch["epoch"]=h.generation()+1;
    beforePresentationReject=h.status()["rejected"].get<unsigned>();raw(guest,wrongEpoch,1);
    check(wait([&]{tick();return h.status()["rejected"].get<unsigned>()>beforePresentationReject;}),"presentation from a previous or different scene cannot enter the adapter");
    auto impersonation=stalePresentation;impersonation["seq"]=101ull;impersonation["peer"]=1;
    beforePresentationReject=h.status()["rejected"].get<unsigned>();raw(guest,impersonation,1);
    check(wait([&]{tick();return h.status()["rejected"].get<unsigned>()>beforePresentationReject;}),"guest cannot impersonate another player's roster or effects");
    check(g.snapshot()["entities"][0].contains("life")&&g.snapshot()["entities"][0].contains("lease"),
        "the host snapshot identifies an entity spawn and its control lease");
    g.publishGrowth(guest,growthModel,growth);
    check(wait([&]{tick();return h.requestedGrowth(2)==growth;}),"the host receives public growth before admitting a guest");
    check(!h.recordGrowth(2,h.playerEntity(2),h.generation(),growth),"a growth request alone is not a confirmed native profile");
    check(g.snapshot().at("entities")[0].at("hp")==100,"late state contains authoritative HP");
    auto incrementalScene=scene();incrementalScene["entities"][0]["hp"]=90;
    check(h.publish(host,incrementalScene,true)&&wait([&]{tick();return g.snapshot()["entities"][0]["hp"]==90;}),"a complete baseline establishes delta recovery state");
    incrementalScene["entities"][0]["hp"]=80;
    check(wait([&]{tick();h.publish(host,incrementalScene);return g.snapshot()["entities"][0]["hp"]==80;})&&
        h.status()["transport"]["deltas_sent"].get<int>()>0,"the host sends and the guest applies a field delta over the real room connection");
    raw(host,{{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","snapshot_delta"},{"epoch",h.generation()},
        {"revision",g.worldRevision()+1},{"base_revision",0},{"delta",Json::array()}},2);
    check(wait([&]{tick();return g.status()["transport"]["baseline_missing"].get<bool>();})&&!g.canAct(2),"a delta without its exact baseline suspends guest operations");
    check(h.publish(host,scene(),true)&&wait([&]{tick();return !g.status()["transport"]["baseline_missing"].get<bool>();}),"a full baseline recovers from a missing delta base");
    check(!g.canAct(2)&&!g.submitSkill(guest,"map01_lv001:level:100","normal"),"snapshot alone cannot admit a player or settle combat");
    advertisedAppearance=appearance;
    check(wait([&]{tick();return h.requestedAppearance(2)==appearance;}),"host receives a guest's public weapon template without native admission");
    check(!h.canAct(2)&&!g.canAct(2),"appearance metadata cannot grant combat or entry readiness");
    check(wait([&]{tick();h.publish(host,scene(),true);return g.requestedAppearance(2)==appearance;}),"host relays appearance through its validated world snapshot");
    const Json hostAppearance={{"template","chr_test"},{"weapon_template",8001}};
    check(h.publishAppearance(host,growthModel,hostAppearance)&&wait([&]{tick();h.publish(host,scene(),true);return g.requestedAppearance(1)==hostAppearance;}),
        "a guest also receives the host's distinct weapon profile");
    auto rejectAppearance=[&](Json extra,const char* name) {
        const auto prior=h.status()["rejected"].get<int>();
        Json body={{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","character_appearance"},
            {"epoch",h.generation()},{"model",growthModel},{"appearance",appearance}};
        body.update(extra);raw(guest,body,1);
        check(wait([&]{tick();return h.status()["rejected"].get<int>()>prior;})&&h.requestedAppearance(2)==advertisedAppearance,name);
    };
    auto privateAppearance=appearance;privateAppearance["weapon_id"]=123456;
    rejectAppearance({{"appearance",privateAppearance}},"account weapon instance IDs are rejected before relay");
    rejectAppearance({{"model","Assets/chr_other_postmodel.prefab"}},"appearance cannot claim another operator model");
    rejectAppearance({{"epoch",99}},"appearance from another world generation is rejected");
    auto invalidSnapshot=g.snapshot();invalidSnapshot["players"][1]["appearance"]=privateAppearance;
    check(!validPlayers(invalidSnapshot["players"],h.generation()),"invalid appearance in a host snapshot is rejected too");
    advertisedAppearance=nullptr;
    check(wait([&]{tick();return h.requestedAppearance(2).is_null();}),"unknown metadata immediately clears a previous weapon profile");
    advertisedAppearance=appearance;
    check(wait([&]{tick();return h.requestedAppearance(2)==appearance;}),"a later complete profile restores only display metadata");
    ReplicaFacts complete{true,true,true,true};
    check(wait([&]{tick();g.reportEntry(guest,complete);return !g.status()["entry"]["blockers"].empty();})&&
        !g.canAct(2),"host without an authority adapter cannot admit a fully prepared guest");
    // A protocol-only fixture explicitly supplies capabilities. The game adapter
    // must prove them through native implementation; production currently cannot.
    h.setAuthority({true,true,true,true,true});
    tick();g.reportEntry(guest,complete);
    check(!h.canAct(2),"authority flags cannot substitute for a bound native player actor");
    const auto guestEntity=h.playerEntity(2);
    check(guestEntity&&h.playerEntity(1)!=guestEntity&&g.playerEntity(2)==guestEntity,"host assigns distinct entity IDs and guest uses the host's ID");
    const std::map<std::string,Realm::SkillRule> skills{{"normal",{1000,5}}};
    check(h.bindPlayer(1,growthModel,skills)&&h.bindPlayer(2,growthModel,skills),"protocol fixture binds host and guest actors with a host-owned skill catalog");
    check(!h.bindPlayer(2,growthModel,skills),"repeated actor binding cannot reset cooldown state");
    const auto existingAppearanceActor=h.playerEntity(2);
    advertisedAppearance["weapon_template"]=8003;
    check(wait([&]{tick();return h.requestedAppearance(2)==advertisedAppearance;})&&h.playerEntity(2)==existingAppearanceActor&&h.nativeState(2).has_value(),
        "weapon appearance updates preserve native actor identity and combat binding");
    check(!h.recordGrowth(2,h.playerEntity(1),h.generation(),growth)&&h.recordGrowth(2,h.playerEntity(2),h.generation(),growth),"only the bound native identity confirms a player's reconstructed growth");
    check(!g.observePlayer(2,guestEntity,g.generation(),nativeFixture),"a guest adapter cannot supply authoritative player HP/position");
    check(!h.observePlayer(2,h.playerEntity(1),h.generation(),nativeFixture),"native observations cannot be attached to another player's identity");
    auto invalidState=nativeFixture;invalidState.hp=-1;
    check(!h.observePlayer(2,guestEntity,h.generation(),invalidState),"invalid native player HP is rejected");
    tick();
    h.publish(host,scene(),true);
    check(wait([&]{tick();g.reportEntry(guest,complete);return g.canAct(2)&&h.canAct(2);}),"prepared guest becomes active only after host commit");
    h.setAuthority({true,true,true,true});
    check(!h.canAct(1)&&!h.canAct(2)&&h.canObservePlayer(1),"native hooks alone cannot prove host damage verification; observation remains available");
    check(wait([&]{tick();g.reportEntry(guest,complete);return !g.canAct(2);})&&g.canObservePlayer(2),
        "missing native damage verification withdraws gameplay grants without disabling read-only cast observation");
    h.setAuthority({true,true,true,true,true});
    check(wait([&]{tick();g.reportEntry(guest,complete);return g.canAct(2)&&h.canAct(2);}),"the protocol fixture explicitly supplies verified native damage evidence");
    check(g.requestedGrowth(2)==growth,"guests use the host-confirmed profile rather than a peer's claim");
    const auto castLife=h.entityReference("map01_lv001:level:100").at("life");
    Json cast={{"cast",1},{"phase","begin"},{"skill","normal"},{"key","map01_lv001:level:100"},{"life",castLife}};
    check(g.publishPlayerCast(guest,cast),"a native guest can announce an observed skill without a damage result");
    std::deque<Json> playerCasts;
    check(wait([&]{tick();playerCasts=h.takePlayerCasts();return !playerCasts.empty();})&&playerCasts.front()["actor"]==guestEntity,
        "the host associates the observation with its bound guest actor");
    if(playerCasts.empty()) {host.stop();guest.stop();late.stop();return 1;}
    auto observedCast=playerCasts.front();observedCast["kind"]="player_cast";observedCast["schema"]=coopWorldSchema;observedCast["room"]=host.snapshot().roomId;
    auto castPrior=h.status()["rejected"].get<int>();raw(guest,observedCast,1);
    check(wait([&]{tick();return h.status()["rejected"].get<int>()>castPrior;})&&h.takePlayerCasts().empty(),"the same observed skill cannot start twice");
    check(h.publishPlayerCast(host,cast)&&h.status()["player_casts"]["active"].size()==2,"the host and guest can use the same skill and cast number independently");
    h.publish(host,scene(),true);
    check(wait([&]{tick();return g.snapshot().contains("casts")&&g.snapshot()["casts"].size()==2;}),"a full world baseline includes current player cast identities");
    auto badCastBaseline=g.snapshot();badCastBaseline["casts"][0]["actor"]=999;
    check(!validCastBaseline(badCastBaseline,h.generation()),"a cast baseline cannot substitute another player's actor");
    cast["phase"]="interrupt";check(g.publishPlayerCast(guest,cast),"the native guest can announce an interruption");
    check(wait([&]{tick();playerCasts=h.takePlayerCasts();return !playerCasts.empty();})&&playerCasts.front()["phase"]=="interrupt"&&
        h.status()["player_casts"]["active"].size()==1,"interrupting the guest leaves the host's concurrent cast active");
    cast["phase"]="end";check(h.publishPlayerCast(host,cast)&&h.status()["player_casts"]["active"].empty(),"each native caster has an independent terminal event");
    g.setAdapterVersion("different-game");
    check(wait([&]{tick();return !h.protocolCompatible(2);})&&!g.canAct(2)&&!h.canAct(2),"a different game version immediately revokes cooperation permission");
    g.setAdapterVersion("fixture-game");
    check(wait([&]{tick();g.reportEntry(guest,complete);return g.canAct(2)&&h.canAct(2);}),"matching game versions require a fresh entry acknowledgement");
    g.setLocalSceneReady(false);
    check(!g.canAct(2)&&!g.publishPlayerCast(guest,cast)&&!g.submitHit(guest,"map01_lv001:level:100",5),
        "native loading immediately revokes the previous scene's skill and hit permissions");
    check(wait([&]{tick();g.reportEntry(guest,complete);return !h.canAct(2);}),"loading cannot advertise cached complete replica facts");
    g.setLocalSceneReady(true);
    check(!g.canAct(2)&&wait([&]{tick();g.reportEntry(guest,complete);return g.canAct(2)&&h.canAct(2);}),
        "a initialized native scene requires a fresh authority acknowledgement");
    g.followHost=false;check(!g.canAct(2),"declining world following immediately revokes operations");
    g.reportEntry(guest,complete);g.followHost=true;
    check(!g.canAct(2)&&wait([&]{tick();g.reportEntry(guest,complete);return g.canAct(2)&&h.canAct(2);}),
        "re-enabling world following cannot reuse the disabled entry grant");
    auto oversized=scene();oversized["padding"]=std::string(proto::kMaxReliableData,'x');
    check(!h.publish(host,oversized,true)&&!h.canAct(1)&&!h.canAct(2)&&h.status()["transport"]["publish_blocked"],
        "an unsendable world immediately revokes host settlement and guest control");
    check(wait([&]{tick();g.reportEntry(guest,complete);return !g.canAct(2);}),"a blocked host cannot renew an operation grant");
    check(h.publish(host,scene(),true)&&wait([&]{tick();g.reportEntry(guest,complete);return g.canAct(2)&&h.canAct(2);}),
        "transport recovery sends a full baseline and requires a fresh guest acknowledgement");
    invalidGrowth=growth;invalidGrowth["level"]=51;
    auto growthRejected=h.status()["rejected"].get<int>();
    raw(guest,{{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","character_growth"},{"model",growthModel},{"growth",invalidGrowth}},1);
    check(wait([&]{tick();return h.status()["rejected"].get<int>()>growthRejected;})&&h.requestedGrowth(2)==growth,"an active player cannot change growth to recreate its actor and reset cooldowns");
    auto r=g.status().at("revision");auto bad=scene();bad["entities"][0]["hp"]=-1;
    raw(host,{{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","snapshot"},{"epoch",1},{"revision",99},{"world",bad}});
    check(wait([&]{tick();return g.status()["rejected"].get<int>()>0;})&&g.status()["revision"]==r,"negative HP cannot replace the world");
    bad=scene();bad["entities"].push_back(bad["entities"][0]);check(!validWorld(bad),"duplicate entity identifiers are rejected");
    bad=scene();bad["entities"][0]["rotation"]={0,0,0,0};check(!validWorld(bad),"invalid rotation rejected");
    late.join("127.0.0.1:47841","");check(wait([&]{return late.currentRole()==Role::Client&&host.snapshot().peers.size()==3;}),"late peer joins");
    check(wait([&]{tick();h.publish(host,scene(),true);return l.ready();}),"late peer gets complete current world");
    check(h.nativeState(1).has_value(),"a late join does not erase the host's native observation or binding");
    check(!l.canAct(3)&&!h.canAct(3),"late joiner must complete its own entry handshake");
    check(wait([&]{tick();h.publish(host,scene(),true);return l.requestedAppearance(2)==advertisedAppearance;}),"late joiner receives current public appearance without account inventory");
    ReplicaFacts partial{true,true,false,false};
    check(wait([&]{tick();l.reportEntry(late,partial);return !l.status()["entry"]["blockers"].empty();})&&!l.canAct(3),
        "matched native entities do not prove guest-world isolation");
    raw(guest,{{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","snapshot"},{"epoch",90},{"revision",1},{"world",scene()}},proto::kEveryone);
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
    raw(guest,{{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","skill_intent"},{"epoch",1},{"seq",seq},{"actor",guestEntity},{"key","map01_lv001:level:100"},{"skill","normal"},{"life",h.entityReference("map01_lv001:level:100").at("life")}} ,1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));tick();
    check(h.takeCommands().empty()&&h.status()["skill_intents_accepted"]==accepted,"replayed skill cannot apply twice");
    auto before=h.status()["rejected"].get<int>();
    raw(guest,{{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","skill_intent"},{"epoch",1},{"seq",100},{"actor",guestEntity},{"key","missing"},{"skill","normal"},{"life",1}},1);
    check(wait([&]{tick();return h.status()["rejected"].get<int>()>before;})&&h.takeCommands().empty(),"unknown target is rejected");
    std::uint64_t rejectedSequence=100;
    auto rejectIntent=[&](Json extra,const char* name) {
        auto prior=h.status()["rejected"].get<int>();
        Json body={{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","skill_intent"},{"epoch",1},{"seq",++rejectedSequence},
            {"actor",guestEntity},{"key","map01_lv001:level:100"},{"skill","normal"},{"life",h.entityReference("map01_lv001:level:100").at("life")}};
        body.update(extra);raw(guest,body,1);
        check(wait([&]{tick();return h.status()["rejected"].get<int>()>prior;})&&h.takeCommands().empty(),name);
    };
    rejectIntent({{"actor",h.playerEntity(1)}},"guest cannot cast as the host's actor");
    rejectIntent({{"amount",999999999}},"client-computed damage is rejected");
    rejectIntent({{"skill","unowned_skill"}},"guest cannot invent a skill absent from the host's catalog");
    rejectIntent({{"life",9999}},"a skill intent cannot target a recycled entity using its old spawn identity");
    rejectIntent(Json::object(),"host enforces its skill cooldown");
    check(!g.submitHit(guest,"map01_lv001:level:100",12.5),"the retired numeric API cannot submit an authoritative damage result");
    auto claimCast=cast;claimCast["cast"]=2;claimCast["phase"]="begin";
    check(g.publishPlayerCast(guest,claimCast)&&wait([&]{tick();return h.status()["player_casts"]["active"].size()==1;}),
        "a hit must first belong to an observed caster and cast instance");
    check(g.submitHitClaim(guest,"map01_lv001:level:100",2,1,0,0),"a verified protocol fixture submits a bounded hit occurrence without damage");
    std::deque<Json> hits;check(wait([&]{tick();hits=h.takeHits();return !hits.empty();})&&hits.front()["from"]==2&&
        hits.front()["cast"]==2&&!hits.front().contains("amount")&&h.snapshot()["entities"][0]["hp"]==100,
        "accepting a hit claim does not subtract HP or manufacture a native result");
    std::uint64_t hitSeq=1000;
    auto rejectHit=[&](Json extra,const char* name) {
        auto prior=h.status()["rejected"].get<int>();
        Json body={{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","hit_claim"},{"epoch",1},{"seq",++hitSeq},{"key","map01_lv001:level:100"},
            {"actor",guestEntity},{"life",h.entityReference("map01_lv001:level:100").at("life")},{"cast",2},{"strike",2},{"action",0},{"unit",0}};
        body.update(extra);raw(guest,body,1);
        check(wait([&]{tick();return h.status()["rejected"].get<int>()>prior;})&&h.takeHits().empty(),name);
    };
    rejectHit({{"amount",101}},"even a bounded damage amount is forbidden in a hit claim");
    rejectHit({{"kind","hit"}},"legacy numeric damage packets cannot enter native settlement");
    rejectHit({{"key","missing"}},"hits on unknown enemies are rejected");
    rejectHit({{"epoch",7}},"hits from another world epoch are rejected");
    rejectHit({{"seq",1}},"replayed hits are rejected");
    rejectHit({{"life",9999}},"a recycled entity key cannot accept an earlier spawn's hit");
    rejectHit({{"actor",h.playerEntity(1)}},"a hit cannot claim another player's actor");
    rejectHit({{"cast",99}},"a hit cannot invent an unobserved cast instance");
    rejectHit({{"strike",1}},"changing the packet sequence cannot replay the same native hit occurrence");
    rejectHit({{"action",-1}},"an unadapted native hit unit cannot be guessed");
    check(!h.sendPlayerHit(host,2,30,"map01_lv001:level:100")&&!g.sendPlayerHit(guest,1,25,"map01_lv001:level:100"),
        "numeric player damage is retired on both host and guest");
    const Json moved=Json::array({{{"key","map01_lv001:level:100"},{"position",{2,0,3}},{"rotation",{0,0,0,1}}}});
    check(!g.sendEnemyStates(guest,moved)&&!g.reportEnemyCast(guest,"map01_lv001:level:100","enemy_attack1",2),
        "a guest cannot publish autonomous AI state or native cast results");
    for(const char* kind:{"player_hit","enemy_state","enemy_cast"}) {
        auto count=h.status()["rejected"].get<unsigned>();
        raw(guest,{{"kind",kind},{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"epoch",h.generation()},
            {"amount",30},{"seq",9999}},1);
        check(wait([&]{tick();return h.status()["rejected"].get<unsigned>()>count;})&&h.takePlayerHits().empty()&&h.takeEnemyStates().empty()&&h.takeEnemyCasts().empty(),
            "legacy result packets cannot enter settlement even through raw transport");
    }
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
    check(h.requestedAppearance(2).is_null(),"map changes discard previous-world appearance before new metadata arrives");
    check(wait([&]{tick();return g.generation()==2&&l.generation()==2;}),"host map changes create a new world epoch for every guest");
    check(!g.canAct(2)&&!h.canAct(2),"map change revokes entry until the new world is prepared");
    check(h.playerEntity(2)!=guestEntity&&!h.canAct(1),"scene change replaces actor IDs and removes every native actor binding");
    raw(host,{{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","entry_result"},{"epoch",1},{"token",1},{"active",true},{"blockers",Json::array()}},2);
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
    const auto previousOperator=h.playerEntity(2);auto switchedInfo=info;switchedInfo.modelPath="Assets/chr_other_postmodel.prefab";guest.setLocalInfo(switchedInfo);
    check(wait([&]{tick();return h.playerEntity(2)!=previousOperator;})&&!h.canAct(2),"operator switching creates a new actor identity and immediately revokes its old entry");
    h.publish(host,scene("map02","map02_lv001"),true);
    check(wait([&]{tick();return g.playerEntity(2)==h.playerEntity(2);})&&!g.canAct(2),"a guest cannot reuse admission granted to its previous operator");
    check(h.bindPlayer(2,switchedInfo.modelPath,skills)&&!h.canAct(2),"a freshly bound operator still needs its own entry acknowledgement");
    h.unbindPlayer(2);check(!h.canAct(2),"losing the native guest actor immediately revokes its entry");
    h.setAuthority({});check(!h.canAct(2)&&!h.canAct(1),"withdrawn host capabilities immediately stop authoritative settlement");
    raw(host,{{"schema",coopWorldSchema},{"room",host.snapshot().roomId},{"kind","snapshot"},{"epoch",1},{"revision",999},{"world",scene()}});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));tick();
    check(g.snapshot()["map"]=="map02"&&g.generation()==2,"old-map snapshots cannot corrupt a new map");
    const auto priorSceneActor=h.playerEntity(1);const auto priorSceneEpoch=h.generation();h.invalidateNativeScene();
    check(!h.ready()&&!h.canAct(1)&&!h.publish(host,scene("map02","map02_lv001"),true),
        "a native same-level reload immediately withdraws old scene operations and publication");
    h.setLocalSceneReady(true);
    check(h.publish(host,scene("map02","map02_lv001"),true)&&h.generation()==priorSceneEpoch+1&&h.playerEntity(1)!=priorSceneActor&&
        wait([&]{tick();return g.generation()==h.generation();})&&!g.canAct(2),
        "same map names cannot reuse the scene epoch or native actor admission after a reload");
    guest.leave();check(wait([&]{tick();return guest.currentRole()==Role::Offline&&!g.ready()&&!g.canAct(2);}),"leave clears guest world and readiness");
    host.stop();guest.stop();late.stop();
    return failures?1:0;
}
