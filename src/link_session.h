#pragma once
// Host-owned world state. This layer contains no Unity pointers and can also be used by
// external world adapters. All state changes occur on the game tick; network callbacks
// only put bounded, copied packets in the inbox.
#include "link_net.h"
#include "link_entry.h"
#include "link_realm.h"
#include "link_coop_contract.h"
#include "link_world_delta.h"
#include "link_cast_lifecycle.h"
#include "link_hit_provenance.h"
#include "link_version.h"
#include "link_presentation.h"
#include "link_presentation_codec.h"
#include "link_resource_authority.h"
#include <optional>
#include "nlohmann/json.hpp"

namespace eflink {
using Json=nlohmann::json;
inline const std::uint32_t worldChannel=proto::fnv32("eflink.world.v1");
inline bool validVector(const Json& value) {
    if(!value.is_array()||value.size()!=3) return false;
    for(const auto& v:value) if(!v.is_number()||!std::isfinite(v.get<double>())||std::abs(v.get<double>())>1000000) return false;
    return true;
}
inline bool validWorld(const Json& w,bool requireIdentities=false) {
    try {
        if(!w.is_object()||!w.at("map").is_string()||w.at("map").get<std::string>().size()>64||
           !w.at("level").is_string()||w.at("level").get<std::string>().size()>64||!validVector(w.at("position"))||
           !w.at("level_num").is_number_integer()||w.at("level_num").get<int>()<0) return false;
        const auto& entities=w.at("entities");if(!entities.is_array()||entities.size()>512) return false;
        std::set<std::string> keys;
        for(const auto& e:entities) {
            auto key=e.at("key").get<std::string>();if(key.empty()||key.size()>128||!keys.insert(key).second||!validVector(e.at("position"))) return false;
            if(requireIdentities&&(!positiveRoomId(e.at("life"))||!positiveRoomId(e.at("lease"))||
                !positiveRoomId(e.at("owner"))||e.at("owner").get<std::uint64_t>()>65535))return false;
            const double hp=e.at("hp").get<double>(),max=e.at("max_hp").get<double>();
            if(!std::isfinite(hp)||!std::isfinite(max)||hp<0||max<=0||max>1e12||hp>max*2||
                e.value("template",std::string{}).size()>256) return false;
            if(e.contains("poise")&&!validPoise(e.at("poise")))return false;
            if(e.contains("enemy_level")&&(!e.at("enemy_level").is_number_integer()||e.at("enemy_level").get<int>()<1||e.at("enemy_level").get<int>()>1000))return false;
            if(e.contains("rotation")) {const auto& q=e["rotation"];if(!q.is_array()||q.size()!=4) return false;
                double length=0;for(const auto& v:q){if(!v.is_number()||!std::isfinite(v.get<double>()))return false;length+=v.get<double>()*v.get<double>();}
                if(length<.5||length>1.5) return false;}
        }
        return true;
    } catch(...) {return false;}
}
inline bool validCastBaseline(const Json& world,std::uint64_t epoch) {
    try {
        if(!world.contains("casts"))return true;
        const auto& casts=world.at("casts");if(!casts.is_array()||casts.size()>RoomCastLedger::maxActive)return false;
        RoomCastLedger validation;
        for(const auto& event:casts) {
            if(event.at("phase")!="begin")return false;
            bool matched=false;
            for(const auto& player:world.at("players"))if(event.at("peer")==player.at("peer")) {
                matched=validation.accept(event,player.at("peer").get<std::uint16_t>(),player.at("entity").get<std::uint64_t>(),epoch,player.at("model").get<std::string>(),0);break;
            }
            if(!matched)return false;
        }
        return true;
    }catch(...) {return false;}
}
class Session {
    struct Packet {std::uint16_t from;Json body;std::size_t size;};
    std::mutex inboxMutex;std::deque<Packet> inbox;std::size_t inboxBytes=0;
    std::uint32_t room=0;Role role=Role::Offline;
    std::uint64_t epoch=0,revision=0,seenAt=0,lastPublish=0,commandSequence=0;
    Json sentWorld;
    std::uint64_t lastBaseline=0,sentRevision=0,baselinesSent=0,deltasSent=0,wireBytes=0;
    bool baselineMissing=false,publishBlocked=false,localSceneReady=false,sceneResetPending=false;
    std::uint64_t congestionSkipped=0;
    std::map<std::uint16_t,std::uint64_t> sequences;
    std::map<std::uint16_t,std::pair<std::uint64_t,unsigned>> attackBudgets;
    std::map<std::uint16_t,Json> locations;std::uint64_t lastLocation=0;
    struct MemberEntry {std::uint64_t epoch=0,token=0,seen=0,actor=0;bool active=false;ReplicaFacts facts;};
    std::map<std::uint16_t,MemberEntry> entries;
    AuthorityFacts authority;ReplicaFacts replica;Realm realm;
    struct Compatibility {bool compatible=false;std::uint64_t seen=0;std::string build;};
    std::map<std::uint16_t,Compatibility> compatibility;
    std::uint64_t lastHello=0;
    RoomEntityLedger entityLedger;
    std::string adapterVersion;
    RoomCastLedger playerCasts;
    HitClaimLedger hitClaimLedger;
    std::uint64_t legacyHitsRejected=0;
    std::deque<Json> castEvents;
    std::uint64_t castEventsAccepted=0;
    std::uint64_t entryToken=0,lastEntryReport=0,lastEntryAck=0;
    bool admitted=false;
    Json entryBlockers=Json::array();
    Json world=Json::object();std::deque<Json> commands;Json teleport=Json::object();
    std::map<std::uint16_t,Json> actorTeleports;
    std::map<std::uint16_t,Json> growthRequests;
    struct Appearance {std::string model;Json profile;std::uint64_t seen=0;};
    std::map<std::uint16_t,Appearance> appearances;
    struct AppearanceApplication {std::uint64_t actor=0,epoch=0,seen=0;Json profile;};
    std::map<std::uint16_t,AppearanceApplication> appliedAppearances;
    std::uint64_t lastAppearancePublish=0,appearancesReceived=0;
    std::string lastAppearanceModel;Json lastAppearance;
    // Hit claims are evidence for the native adapter, never HP deltas.
    std::deque<Json> hits,playerHits,enemyCasts,enemyStates;std::map<std::uint16_t,std::uint64_t> hitSequences;
    std::map<std::uint16_t,std::pair<std::uint64_t,unsigned>> hitBudgets;std::uint64_t hitSequence=0,hitsAccepted=0;
    std::uint64_t lastGrowthPublish=0;
    std::uint64_t eventSequence=0;
    std::map<std::pair<std::uint16_t,std::string>,std::uint64_t> eventSequences;
    bool acceptEvent(std::uint16_t from,const std::string& kind,const Json& body) {
        const auto seq=body.at("seq").get<std::uint64_t>();
        auto& prior=eventSequences[{from,kind}];
        if(!seq||seq<=prior)return false;
        prior=seq;return true;
    }
    bool validEntityEvent(const Json& body,std::uint16_t owner=0) const {
        return positiveRoomId(body.at("life"))&&positiveRoomId(body.at("lease"))&&
            entityMatches(body.at("key").get<std::string>(),body.at("life").get<std::uint64_t>(),
                body.at("lease").get<std::uint64_t>(),owner);
    }
    struct Presentation {Json data;std::uint64_t seen=0;};
    std::map<std::uint16_t,Presentation> presentations;
    std::atomic<std::uint64_t> presentationCoalesced{0};
    std::uint64_t presentationSequence=0,lastPresentation=0,presentationReceived=0,presentationSkipped=0,presentationRawBytes=0,presentationWireBytes=0,presentationPacked=0;
    std::uint64_t rejected=0,received=0,attacks=0;std::string error;
    Json resourceBaseline=Json::array();std::uint64_t resourceRevision=0,lastResourcePublish=0,resourceSeen=0;
    bool send(Net& net,Json body,std::uint16_t to=proto::kEveryone) {
        body["schema"]=coopWorldSchema;body["room"]=room;
        const auto plain=body.dump();const bool presentation=body.value("kind",Json{})=="presentation";
        const auto text=presentation?packPresentation(plain,proto::kMaxReliableData):plain;
        if(text.size()>proto::kMaxReliableData) {error="世界快照超过网络上限";return false;}
        const bool sent=net.sendData(worldChannel,to,text.data(),text.size(),true);
        if(sent&&presentation) {presentationRawBytes+=plain.size();presentationWireBytes+=text.size();if(packedPresentation(text.data(),text.size()))++presentationPacked;}
        return sent;
    }
    const PeerView* find(const Snapshot& s,std::uint16_t id) const {
        for(const auto& p:s.peers) if(p.id==id) return &p;return nullptr;
    }
    bool fresh(const PeerView* p) const {
        return p&&p->hasInfo&&!p->states.empty()&&(p->states.back().state.flags&proto::StateValid)&&nowMs()-p->states.back().receivedMs<3000;
    }
    void routeTeleport(Net& net,const Snapshot& s,std::uint16_t actor,std::uint16_t target,std::uint64_t seq) {
        const auto* p=find(s,target);
        if(!fresh(p)||p->info.mapId.empty()||actor==target) {error="目标玩家尚未进入可传送场景";return;}
        const auto& st=p->states.back().state;
        // A hosted guest may only use the host's active world. The host can change the
        // room's map by travelling to another peer; all guests then follow the new epoch.
        if(actor!=proto::kHostId&&!world.empty()&&(p->info.mapId!=world.value("map",std::string{})||p->info.levelId!=world.value("level",std::string{}))) {
            error="目标还未进入房主世界，请等待地图同步";
            send(net,{{"kind","teleport_error"},{"seq",seq},{"message",error}},actor);return;
        }
        Json request={{"kind","teleport"},{"epoch",epoch},{"seq",seq},{"target",target},
            {"map",p->info.mapId},{"level",p->info.levelId},{"position",{st.position[0],st.position[1],st.position[2]}},
            {"issued",nowMs()}};
        if(auto state=nativeState(target);state&&p->info.mapId==world.value("map",std::string{})&&p->info.levelId==world.value("level",std::string{}))
            request["position"]=state->position;
        if(target==proto::kHostId&&!world.empty()) request["level_num"]=world.value("level_num",0);
        // The target's dungeon (the room world's, when the target is in it): the
        // traveller enters it through the official dungeon flow.
        if(!world.empty()&&p->info.mapId==world.value("map",std::string{})&&p->info.levelId==world.value("level",std::string{}))
            request["dungeon"]=world.value("dungeon",std::string{});
        else if(auto it=locations.find(target);it!=locations.end()&&it->second.value("map",std::string{})==p->info.mapId&&it->second.value("level",std::string{})==p->info.levelId) request["level_num"]=it->second.value("level_num",0);
        if(actor==s.localId) teleport=request;
        else if(send(net,request,actor)&&role==Role::Host&&request.at("map")==world.value("map",std::string{})&&request.at("level")==world.value("level",std::string{}))
            actorTeleports[actor]=request;
    }
public:
    bool followHost=true,combat=true;
    void receive(std::uint16_t from,const void* data,std::size_t size) {
        if(!data||!size||size>proto::kMaxReliableData) return;
        const bool packed=packedPresentation(data,size);std::optional<std::string> decoded;
        if(packed) {decoded=unpackPresentation(data,size,proto::kMaxReliableData);if(!decoded)return;data=decoded->data();size=decoded->size();}
        auto parsed=Json::parse(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+size,nullptr,false);
        if(parsed.is_discarded()||!parsed.is_object()||(packed&&parsed.value("kind",Json{})!="presentation")) return;
        std::lock_guard lock(inboxMutex);
        // Only complete presentation frames may supersede older pending frames.
        // Cast terminals, damage evidence and world deltas retain their order.
        if(parsed.value("kind",Json{})=="presentation"&&parsed.contains("seq")&&parsed.at("seq").is_number_unsigned()) {
            for(auto it=inbox.begin();it!=inbox.end();) {
                if(it->from==from&&it->body.value("kind",Json{})=="presentation"&&
                    it->body.value("room",Json{})==parsed.value("room",Json{})&&
                    it->body.value("epoch",Json{})==parsed.value("epoch",Json{})&&
                    it->body.value("peer",Json{})==parsed.value("peer",Json{})&&
                    it->body.contains("seq")&&it->body.at("seq").is_number_unsigned()&&it->body.at("seq").get<std::uint64_t>()<parsed.at("seq").get<std::uint64_t>()) {
                    inboxBytes-=it->size;it=inbox.erase(it);++presentationCoalesced;
                }else ++it;
            }
        }
        if(inbox.size()>=128||inboxBytes+size>4u*1024*1024) return;
        inbox.push_back({from,std::move(parsed),size});inboxBytes+=size;
    }
    void update(Net& net) {
        const auto s=net.snapshot();
        if(s.roomId!=room||s.role!=role) {
            room=s.roomId;role=s.role;epoch=revision=seenAt=lastPublish=commandSequence=0;world=Json::object();
            sentWorld=nullptr;lastBaseline=sentRevision=0;baselineMissing=publishBlocked=sceneResetPending=false;
            presentations.clear();presentationSequence=lastPresentation=0;
            resourceBaseline=Json::array();resourceRevision=lastResourcePublish=resourceSeen=0;
            playerCasts.clear();castEvents.clear();hitClaimLedger.clear();
            hits.clear();playerHits.clear();enemyCasts.clear();enemyStates.clear();hitSequences.clear();hitBudgets.clear();hitSequence=0;
            sequences.clear();attackBudgets.clear();locations.clear();lastLocation=0;commands.clear();teleport=Json::object();actorTeleports.clear();error.clear();
            entries.clear();replica={};entryToken=lastEntryReport=lastEntryAck=0;admitted=false;entryBlockers=Json::array();
            realm.reset(room);
            compatibility.clear();lastHello=0;entityLedger.clear();
            eventSequences.clear();eventSequence=0;
            growthRequests.clear();lastGrowthPublish=0;
            appearances.clear();lastAppearancePublish=0;lastAppearanceModel.clear();lastAppearance=nullptr;
            appliedAppearances.clear();
        }
        if(role==Role::Host&&ready()) realm.reconcile(s,epoch,world.value("map",std::string{}),world.value("level",std::string{}));
        if(ready()) {
            std::set<std::uint64_t> current;
            const auto players=role==Role::Host?realm.json():world.value("players",Json::array());
            for(const auto& player:players)current.insert(player.at("entity").get<std::uint64_t>());
            playerCasts.retain(current);
        }
        for(auto it=compatibility.begin();it!=compatibility.end();) {
            if(!find(s,it->first))it=compatibility.erase(it);else ++it;
        }
        if((role==Role::Host||role==Role::Client)&&(!lastHello||nowMs()-lastHello>=1000)) {
            if(send(net,{{"kind","coop_hello"},{"world_schema",coopWorldSchema},{"contract_revision",coopContractRevision},
                {"contract",coopContractName},{"build",EFLINK_BUILD_ID},{"game_version",adapterVersion}},role==Role::Host?proto::kEveryone:proto::kHostId))lastHello=nowMs();
        }
        for(auto it=entries.begin();it!=entries.end();) {
            if(!find(s,it->first)||(it->second.active&&!realm.bound(it->first)))it=entries.erase(it);else ++it;
        }
        for(auto it=growthRequests.begin();it!=growthRequests.end();) {
            const auto* p=find(s,it->first);
            if(!p||!validCharacterGrowth(it->second,p->info.modelPath))it=growthRequests.erase(it);else ++it;
        }
        for(auto it=appearances.begin();it!=appearances.end();) {
            const auto* p=find(s,it->first);
            if(!fresh(p)||p->info.modelPath!=it->second.model||p->info.mapId!=world.value("map",std::string{})||
               p->info.levelId!=world.value("level",std::string{})||nowMs()-it->second.seen>3000)it=appearances.erase(it);
            else ++it;
        }
        std::deque<Packet> pending;
        {std::lock_guard lock(inboxMutex);pending.swap(inbox);inboxBytes=0;}
        for(const auto& packet:pending) {
            try {
                const auto& b=packet.body;
                if(!room||b.at("schema")!=coopWorldSchema||b.at("room").get<std::uint32_t>()!=room||!find(s,packet.from)) {++rejected;continue;}
                const auto kind=b.at("kind").get<std::string>();
                if(kind=="coop_hello") {
                    if(packet.from==s.localId||(role==Role::Client&&packet.from!=proto::kHostId)||
                        (role!=Role::Host&&role!=Role::Client)) {++rejected;continue;}
                    const bool compatible=validCoopHello(b)&&b.at("build")==EFLINK_BUILD_ID&&b.at("game_version")==adapterVersion;
                    const std::string build=b.contains("build")&&b.at("build").is_string()?b.at("build").get<std::string>().substr(0,64):std::string{};
                    compatibility[packet.from]={compatible,nowMs(),build};
                    if(!compatible) {++rejected;entries.erase(packet.from);if(role==Role::Client)admitted=false;}
                } else if(kind=="resource_baseline") {
                    if(b.size()!=8||role!=Role::Client||packet.from!=proto::kHostId||!ready()||!localSceneOperational()||!followHost||!combat||!protocolCompatible(proto::kHostId)||
                       b.at("epoch")!=epoch||b.at("map")!=world.at("map")||b.at("level")!=world.at("level")||
                       !positiveRoomId(b.at("revision"))||b.at("revision").get<std::uint64_t>()<=resourceRevision||!validResourceBaseline(b.at("objects"))) {++rejected;continue;}
                    resourceBaseline=b.at("objects");resourceRevision=b.at("revision").get<std::uint64_t>();resourceSeen=nowMs();
                } else if(kind=="presentation") {
                    const auto owner=b.at("peer").get<std::uint16_t>();const auto* p=find(s,owner);
                    if(!ready()||!protocolCompatible(role==Role::Host?packet.from:proto::kHostId)||
                       (role==Role::Host?packet.from!=owner:packet.from!=proto::kHostId)||owner==s.localId||owner>=8192||!fresh(p)||
                       b.at("epoch")!=epoch||b.at("map")!=world.at("map")||b.at("level")!=world.at("level")||
                       p->info.mapId!=world.at("map").get<std::string>()||p->info.levelId!=world.at("level").get<std::string>()||!validPresentation(b)) {++rejected;continue;}
                    auto& prior=presentations[owner];
                    if(!prior.data.empty()&&prior.data.value("epoch",std::uint64_t{})==epoch&&b.at("seq").get<std::uint64_t>()<=prior.data.at("seq").get<std::uint64_t>()) {++rejected;continue;}
                    bool valid=true;for(const auto& m:b.at("members"))
                        if(!validCharacterGrowth(m.at("growth"),m.at("model").get<std::string>())||
                           (!m.at("appearance").is_null()&&!validCharacterAppearance(m.at("appearance"),m.at("model").get<std::string>())))valid=false;
                    if(!valid){++rejected;continue;}
                    prior={b,nowMs()};++presentationReceived;
                    if(role==Role::Host&&net.transmitPressure()<64u*1024)send(net,b);
                } else if(kind=="snapshot"||kind=="snapshot_delta") {
                    const auto e=b.at("epoch").get<std::uint64_t>(),r=b.at("revision").get<std::uint64_t>();
                    if(role!=Role::Client||packet.from!=proto::kHostId||!e||e<epoch||(e==epoch&&r<=revision)) {++rejected;continue;}
                    Json candidate;
                    if(kind=="snapshot_delta") {
                        if(baselineMissing||e!=epoch||b.at("base_revision")!=revision) {baselineMissing=true;admitted=false;++rejected;continue;}
                        try {candidate=applyWorldDelta(world,b.at("delta"));}catch(...) {baselineMissing=true;admitted=false;++rejected;continue;}
                    }else candidate=b.at("world");
                    if(!validWorld(candidate,true)||!validPlayers(candidate.at("players"),e)||!validCastBaseline(candidate,e)) {
                        if(kind=="snapshot_delta") {baselineMissing=true;admitted=false;}
                        ++rejected;continue;
                    }
                    std::uint64_t candidateActor=0;bool candidateBound=false;
                    for(const auto& player:candidate.at("players"))if(player.at("peer")==s.localId) {
                        candidateActor=player.at("entity").get<std::uint64_t>();candidateBound=player.value("native_bound",false)&&player.value("in_scene",false);
                    }
                    if(e!=epoch||candidateActor!=playerEntity(s.localId)||(admitted&&!candidateBound)) {teleport=Json::object();commands.clear();replica={};admitted=false;playerHits.clear();enemyCasts.clear();
                        if(e!=epoch) {playerCasts.clear();castEvents.clear();hitClaimLedger.clear();resourceBaseline=Json::array();resourceRevision=resourceSeen=0;}
                        entryToken=nowMs()^(std::uint64_t(room)<<32)^e; if(!entryToken)entryToken=1;
                        lastEntryReport=lastEntryAck=0;entryBlockers=Json::array();}
                    epoch=e;revision=r;world=std::move(candidate);seenAt=nowMs();baselineMissing=false;++received;
                    for(const auto& event:world.value("casts",Json::array())) {
                        const auto peer=event.at("peer").get<std::uint16_t>();
                        for(const auto& player:world.at("players"))if(player.at("peer")==peer&&castEvents.size()<256) {
                            if(playerCasts.accept(event,peer,playerEntity(peer),epoch,player.at("model").get<std::string>(),nowMs()))castEvents.push_back(event);
                            break;
                        }
                    }
                } else if(kind=="entry_prepare"&&role==Role::Host) {
                    const auto e=b.at("epoch").get<std::uint64_t>(),token=b.at("token").get<std::uint64_t>();
                    const auto r=b.at("revision").get<std::uint64_t>();
                    if(!ready()||!protocolCompatible(packet.from)||e!=epoch||!token||!r||r>revision||packet.from==proto::kHostId||
                        b.at("map")!=world.at("map")||b.at("level")!=world.at("level")) {++rejected;continue;}
                    auto facts=ReplicaFacts::parse(b.at("replica"));
                    auto blocks=authority.blockers();for(const auto& x:facts.blockers()) blocks.push_back(x);
                    if(publishBlocked)blocks.push_back("host_transport_blocked");
                    if(!localSceneReady||!followHost||!combat)blocks.push_back("host_local_scene");
                    const auto* actor=realm.find(packet.from);
                    if(!actor||!realm.bound(packet.from)) blocks.push_back("host_player_entity");
                    if(!actor||b.at("actor").get<std::uint64_t>()!=actor->entity) {++rejected;continue;}
                    const bool active=localSceneReady&&followHost&&combat&&!publishBlocked&&authority.complete()&&facts.complete()&&realm.bound(packet.from);
                    entries[packet.from]={epoch,token,nowMs(),actor->entity,active,facts};
                    send(net,{{"kind","entry_result"},{"epoch",epoch},{"token",token},{"actor",actor->entity},{"active",active},{"blockers",blocks}},packet.from);
                } else if(kind=="entry_result"&&role==Role::Client&&packet.from==proto::kHostId) {
                    const auto& blocks=b.at("blockers");
                    if(!ready()||!protocolCompatible(proto::kHostId)||b.at("epoch").get<std::uint64_t>()!=epoch||b.at("token").get<std::uint64_t>()!=entryToken||
                       !entryToken||b.at("actor").get<std::uint64_t>()!=playerEntity(s.localId)||!blocks.is_array()||blocks.size()>16) {++rejected;continue;}
                    bool valid=true;for(const auto& x:blocks) if(!x.is_string()||x.get<std::string>().size()>128) valid=false;
                    if(!valid) {++rejected;continue;}
                    admitted=b.at("active").get<bool>()&&blocks.empty()&&replica.complete();entryBlockers=blocks;lastEntryAck=nowMs();
                } else if(kind=="player_cast"&&(role==Role::Host?canObservePlayer(packet.from):role==Role::Client&&packet.from==proto::kHostId&&protocolCompatible(proto::kHostId)&&ready())) {
                    if(b.size()!=13||castEvents.size()>=256) {++rejected;continue;}
                    Json event=Json::object();for(const char* field:{"peer","actor","epoch","model","cast","seq","phase","skill","key","life"})event[field]=b.at(field);
                    const auto peer=event.at("peer").get<std::uint16_t>();
                    if(role==Role::Host&&peer!=packet.from) {++rejected;continue;}
                    std::string model;bool bound=false;
                    if(role==Role::Host) {
                        const auto* player=realm.find(peer);if(player) {model=player->model;bound=player->nativeBound&&player->skills.contains(event.at("skill").get<std::string>());}
                    }else for(const auto& player:world.at("players"))if(player.at("peer")==peer) {model=player.at("model").get<std::string>();bound=player.value("native_bound",false)&&player.value("in_scene",false);break;}
                    const auto key=event.at("key").get<std::string>();
                    if(!bound||(event.at("phase")=="begin"&&!key.empty()&&!entityMatches(key,event.at("life").get<std::uint64_t>()))||
                        !playerCasts.accept(event,peer,playerEntity(peer),epoch,model,nowMs())) {++rejected;continue;}
                    castEvents.push_back(event);++castEventsAccepted;
                    if(role==Role::Host) {auto relay=event;relay["kind"]="player_cast";send(net,std::move(relay));}
                } else if(kind=="character_appearance"&&role==Role::Host) {
                    const auto* p=find(s,packet.from);
                    const auto& appearance=b.at("appearance");
                    if(!ready()||!fresh(p)||b.at("epoch").get<std::uint64_t>()!=epoch||b.at("model")!=p->info.modelPath||
                       p->info.mapId!=world.value("map",std::string{})||p->info.levelId!=world.value("level",std::string{})||
                       (!appearance.is_null()&&!validCharacterAppearance(appearance,p->info.modelPath))) {++rejected;continue;}
                    // Display metadata does not recreate an actor or reset combat.
                    if(appearance.is_null())appearances.erase(packet.from);
                    else appearances[packet.from]={p->info.modelPath,appearance,nowMs()};
                    ++appearancesReceived;
                } else if(kind=="character_growth"&&role==Role::Host) {
                    const auto* p=find(s,packet.from);
                    if(!fresh(p)||b.at("model")!=p->info.modelPath||!validCharacterGrowth(b.at("growth"),p->info.modelPath)) {++rejected;continue;}
                    // Growth changes cannot reset an admitted actor's cooldowns.
                    if(canAct(packet.from)&&(!growthRequests.contains(packet.from)||growthRequests.at(packet.from)!=b.at("growth"))) {++rejected;continue;}
                    growthRequests[packet.from]=b.at("growth");
                } else if(kind=="location"&&role==Role::Host) {
                    const auto* p=find(s,packet.from);auto number=b.at("level_num").get<int>();
                    if(!p||b.at("map")!=p->info.mapId||b.at("level")!=p->info.levelId||number<=0) {++rejected;continue;}
                    locations[packet.from]=b;
                } else if(kind=="teleport_request"&&role==Role::Host) {
                    const auto seq=b.at("seq").get<std::uint64_t>();
                    if(!seq||seq<=sequences[packet.from]) {++rejected;continue;}
                    sequences[packet.from]=seq;routeTeleport(net,s,packet.from,b.at("target").get<std::uint16_t>(),seq);
                } else if(kind=="teleport"&&role==Role::Client&&packet.from==proto::kHostId) {
                    if(b.at("epoch").get<std::uint64_t>()!=epoch||!validVector(b.at("position"))||b.at("map").get<std::string>().size()>64||b.at("level").get<std::string>().size()>64) {++rejected;continue;}
                    if(b.contains("dungeon")&&(!b.at("dungeon").is_string()||b.at("dungeon").get<std::string>().size()>96)) {++rejected;continue;}
                    teleport=b;teleport["issued"]=nowMs();
                } else if(kind=="teleport_error"&&role==Role::Client&&packet.from==proto::kHostId) error=b.at("message").get<std::string>().substr(0,512);
                else if(kind=="skill_intent"&&role==Role::Host&&combat) {
                    const auto seq=b.at("seq").get<std::uint64_t>();const auto now=nowMs();
                    const auto skill=b.at("skill").get<std::string>(),key=b.at("key").get<std::string>();
                    const auto* p=find(s,packet.from);
                    if(!canAct(packet.from)||!seq||seq<=sequences[packet.from]||b.at("epoch").get<std::uint64_t>()!=epoch||!fresh(p)||
                       b.at("actor").get<std::uint64_t>()!=playerEntity(packet.from)||!positiveRoomId(b.at("life"))||!entityMatches(key,b.at("life").get<std::uint64_t>())||b.contains("amount")||b.contains("damage")||
                       skill.empty()||skill.size()>128||key.empty()||key.size()>128||commands.size()>=256||
                       p->info.mapId!=world.value("map",std::string{})||p->info.levelId!=world.value("level",std::string{})) {++rejected;continue;}
                    sequences[packet.from]=seq;
                    // A host-owned skill catalog sets range/cooldown. No client damage
                    // value, damage type, stat multiplier or claimed actor is trusted.
                    auto& budget=attackBudgets[packet.from];if(now-budget.first>=1000) budget={now,0};
                    if(budget.second>=20) {++rejected;continue;}
                    const auto* actor=realm.find(packet.from);
                    if(!realm.stateReady(packet.from)||!actor){++rejected;continue;}
                    bool target=false;double distance=0;
                    for(const auto& entity:world.value("entities",Json::array())) if(entity.at("key")==key) {
                        const auto& pos=entity.at("position");
                        for(int i=0;i<3;++i) {const double d=pos[i].get<double>()-actor->state.position[i];distance+=d*d;}
                        target=entity.at("hp").get<double>()>0&&entity.value("alive",true);break;
                    }
                    if(!target||!realm.acceptSkill(packet.from,skill,distance,now)) {++rejected;continue;}
                    ++budget.second;
                    // Build an allowlisted command; never pass arbitrary peer JSON to
                    // the game adapter. It executes as this player's bound actor.
                    commands.push_back({{"kind","skill_intent"},{"epoch",epoch},{"seq",seq},{"from",packet.from},
                        {"actor",playerEntity(packet.from)},{"key",key},{"skill",skill},{"life",b.at("life")}});++attacks;
                } else if(kind=="hit"&&role==Role::Host) {
                    ++legacyHitsRejected;++rejected;
                } else if(kind=="hit_claim"&&role==Role::Host&&combat) {
                    if(b.size()!=12||!positiveRoomId(b.at("seq"))||!canAct(packet.from)||hits.size()>=256) {++rejected;continue;}
                    Json claim=Json::object();for(const char* field:{"actor","epoch","cast","key","life","strike","action","unit"})claim[field]=b.at(field);
                    const auto seq=b.at("seq").get<std::uint64_t>();const auto now=nowMs();
                    if(!validHitClaim(claim)||claim.at("epoch")!=epoch||claim.at("actor")!=playerEntity(packet.from)||
                        seq<=hitSequences[packet.from]||!playerCasts.permitsClaim(claim,packet.from,now)||
                        !entityMatches(claim.at("key").get<std::string>(),claim.at("life").get<std::uint64_t>())) {++rejected;continue;}
                    auto& budget=hitBudgets[packet.from];if(now-budget.first>=1000)budget={now,0};
                    if(budget.second>=40||!hitClaimLedger.accept(claim,now)) {++rejected;continue;}
                    hitSequences[packet.from]=seq;++budget.second;++hitsAccepted;
                    claim["from"]=packet.from;claim["seq"]=seq;hits.push_back(std::move(claim));
                } else if(kind=="player_hit"||kind=="enemy_state") {
                    ++rejected; // retired guest-computed combat results
                } else if(kind=="enemy_cast"&&(role==Role::Client&&packet.from==proto::kHostId&&canAct(s.localId))) {
                    // Host observation only; guests never announce AI results.
                    const auto key=b.at("key").get<std::string>(),skill=b.at("skill").get<std::string>();
                    if(b.at("epoch").get<std::uint64_t>()!=epoch||key.empty()||key.size()>128||skill.empty()||skill.size()>128||enemyCasts.size()>=64||
                        !validEntityEvent(b,role==Role::Host?packet.from:0)||!acceptEvent(packet.from,kind,b)) {++rejected;continue;}
                    enemyCasts.push_back({{"key",key},{"skill",skill},{"target",b.at("target").get<std::uint16_t>()},{"from",packet.from},
                        {"life",b.at("life")},{"lease",b.at("lease")},{"seq",b.at("seq")}});
                } else ++rejected;
            } catch(...) {++rejected;}
        }
    }
    bool publish(Net& net,Json value,bool force=false) {
        if(role!=Role::Host||!localSceneReady||!validWorld(value)) return false;
        if(sceneResetPending||world.empty()||world.value("map",std::string{})!=value.at("map").get<std::string>()||world.value("level",std::string{})!=value.at("level").get<std::string>()) {
            ++epoch;revision=0;commands.clear();hits.clear();teleport=Json::object();actorTeleports.clear();entries.clear();
            appearances.clear();lastAppearancePublish=0;lastAppearanceModel.clear();lastAppearance=nullptr;
            appliedAppearances.clear();
            entityLedger.clear();
            eventSequences.clear();
            sentWorld=nullptr;lastBaseline=sentRevision=0;
            playerCasts.clear();castEvents.clear();hitClaimLedger.clear();
            sceneResetPending=false;
            resourceBaseline=Json::array();resourceRevision=lastResourcePublish=resourceSeen=0;
        }
        entityLedger.annotate(value["entities"]);
        realm.reconcile(net.snapshot(),epoch,value.at("map").get<std::string>(),value.at("level").get<std::string>());
        value["players"]=realm.json();
        value["casts"]=playerCasts.active(nowMs());
        for(auto& player:value["players"]) {
            const auto peer=player.at("peer").get<std::uint16_t>();
            if(auto appearance=requestedAppearance(peer);!appearance.is_null())player["appearance"]=std::move(appearance);
        }
        world=std::move(value);seenAt=nowMs();
        // Coalesce replaceable world states before the reliable queue fills;
        // control grants and key events continue on the reliable connection.
        if(!force&&net.transmitPressure()>256u*1024) {++congestionSkipped;return true;}
        if(!force&&nowMs()-lastPublish<50) return true;
        lastPublish=nowMs();const auto nextRevision=revision+1;
        Json full={{"kind","snapshot"},{"epoch",epoch},{"revision",nextRevision},{"world",world}};
        Json message=full;bool baseline=true;
        if(!force&&!sentWorld.is_null()&&nowMs()-lastBaseline<1000)try {
            Json incremental={{"kind","snapshot_delta"},{"epoch",epoch},{"revision",nextRevision},{"base_revision",sentRevision},{"delta",worldDelta(sentWorld,world)}};
            if(incremental.dump().size()<full.dump().size()*9/10) {message=std::move(incremental);baseline=false;}
        }catch(...) {} // an oversized delta falls back to a complete baseline
        const auto bytes=message.dump().size();
        if(!send(net,std::move(message))) {
            publishBlocked=true;sentWorld=nullptr;
            for(auto& [id,entry]:entries)entry.active=false;
            commands.clear();hits.clear();playerHits.clear();enemyCasts.clear();enemyStates.clear();
            return false;
        }
        publishBlocked=false;
        revision=sentRevision=nextRevision;sentWorld=world;wireBytes+=bytes;
        if(baseline) {lastBaseline=nowMs();++baselinesSent;}else ++deltasSent;
        return true;
    }
    void requestTeleport(Net& net,std::uint16_t target) {
        auto s=net.snapshot();if(s.role!=Role::Host&&s.role!=Role::Client) throw std::runtime_error("请先连接房间");
        ++commandSequence;if(role==Role::Host) routeTeleport(net,s,s.localId,target,commandSequence);
        else if(!send(net,{{"kind","teleport_request"},{"target",target},{"seq",commandSequence}},proto::kHostId)) throw std::runtime_error("传送请求未发送");
    }
    void publishLocation(Net& net,const std::string& map,const std::string& level,int number) {
        if(role!=Role::Client||number<=0||nowMs()-lastLocation<500) return;
        lastLocation=nowMs();send(net,{{"kind","location"},{"map",map},{"level",level},{"level_num",number}},proto::kHostId);
    }
    bool submitSkill(Net& net,const std::string& key,const std::string& skill) {
        if(role!=Role::Client||!canAct(net.snapshot().localId)||!combat) return false;
        const auto reference=entityReference(key);if(reference.is_null())return false;
        return send(net,{{"kind","skill_intent"},{"epoch",epoch},{"seq",++commandSequence},{"actor",playerEntity(net.snapshot().localId)},
            {"key",key},{"skill",skill},{"life",reference.at("life")}},proto::kHostId);
    }
    bool publishPlayerCast(Net& net,Json event) {
        const auto snapshot=net.snapshot();const auto peer=snapshot.localId;
        if(!canObservePlayer(peer)||!combat)return false;
        const auto* player=find(snapshot,peer);if(!player)return false;
        event["peer"]=peer;event["actor"]=playerEntity(peer);event["epoch"]=epoch;event["model"]=player->info.modelPath;event["seq"]=++eventSequence;
        if(role==Role::Host) {
            const auto* native=realm.find(peer);
            if(!native||!native->skills.contains(event.at("skill").get<std::string>())||
                !playerCasts.accept(event,peer,playerEntity(peer),epoch,player->info.modelPath,nowMs()))return false;
        }
        event["kind"]="player_cast";
        return send(net,std::move(event),role==Role::Client?proto::kHostId:proto::kEveryone);
    }
    bool publishPresentation(Net& net,Json value) {
        const auto s=net.snapshot();const auto now=nowMs();
        if(!ready()||!localSceneOperational()||now-lastPresentation<50)return false;
        if(net.transmitPressure()>64u*1024){++presentationSkipped;return false;}
        value.update(Json{{"kind","presentation"},{"version",1},{"peer",s.localId},{"epoch",epoch},
            {"map",world.at("map")},{"level",world.at("level")},{"seq",++presentationSequence}});
        if(!validPresentation(value))return false;
        if(send(net,std::move(value),role==Role::Host?proto::kEveryone:proto::kHostId)){lastPresentation=now;return true;}
        return false;
    }
    bool publishResources(Net& net,const Json& value) {
        if(role!=Role::Host||!ready()||!localSceneOperational()||!followHost||!combat||!validResourceBaseline(value)||resourceRevision==UINT64_MAX)return false;
        if(nowMs()-lastResourcePublish<250||net.transmitPressure()>64u*1024)return false;
        const auto next=resourceRevision+1;
        if(!send(net,{{"kind","resource_baseline"},{"epoch",epoch},{"revision",next},{"map",world.at("map")},{"level",world.at("level")},{"objects",value}}))return false;
        resourceBaseline=value;resourceRevision=next;lastResourcePublish=resourceSeen=nowMs();return true;
    }
    Json resources() const {return ready()&&localSceneOperational()&&followHost&&combat&&resourceSeen&&nowMs()-resourceSeen<=1500?resourceBaseline:Json::array();}
    Json presentation(std::uint16_t peer) const {
        auto it=presentations.find(peer);
        if(it==presentations.end()||nowMs()-it->second.seen>500||it->second.data.value("epoch",std::uint64_t{})!=epoch)return Json{};
        auto result=it->second.data;result["received"]=it->second.seen;return result;
    }
    std::deque<Json> takePlayerCasts() {std::deque<Json> result;result.swap(castEvents);return result;}
    bool submitHit(Net&,const std::string&,double) {return false;} // retired numeric API, never sends damage
    bool submitHitClaim(Net& net,const std::string& key,std::uint64_t cast,std::uint64_t strike,int action,int unit) {
        if(role!=Role::Client||!combat||!canAct(net.snapshot().localId))return false;
        auto ref=entityReference(key);if(ref.is_null())return false;
        ref.erase("lease");ref.erase("owner");
        ref.update(Json{{"epoch",epoch},{"key",key},{"cast",cast},{"strike",strike},{"action",action},{"unit",unit},
            {"actor",playerEntity(net.snapshot().localId)}});
        if(!validHitClaim(ref))return false;
        ref.update(Json{{"kind","hit_claim"},{"seq",++hitSequence}});
        return send(net,std::move(ref),proto::kHostId);
    }
    bool sendPlayerHit(Net&,std::uint16_t,double,const std::string&) {return false;}
    // Presentation: a host room enemy began a native skill against a player.
    bool sendEnemyCast(Net& net,const std::string& key,const std::string& skill,std::uint16_t target) {
        if(role!=Role::Host||!ready()||key.empty()||skill.empty()||skill.size()>128) return false;
        auto ref=entityReference(key);if(ref.is_null())return false;
        ref.update(Json{{"kind","enemy_cast"},{"epoch",epoch},{"seq",++eventSequence},{"key",key},{"skill",skill},{"target",target}});
        return send(net,std::move(ref));
    }
    std::deque<Json> takeEnemyCasts() {std::deque<Json> out;out.swap(enemyCasts);return out;}
    // Guest: report the room enemies whose AI this player runs.
    bool sendEnemyStates(Net&,const Json&) {return false;}
    // Guest: an enemy it owns began a native skill; the host replays and relays it.
    bool reportEnemyCast(Net&,const std::string&,const std::string&,std::uint16_t) {return false;}
    std::deque<Json> takeEnemyStates() {std::deque<Json> out;out.swap(enemyStates);return out;}
    std::deque<Json> takeHits() {std::deque<Json> out;out.swap(hits);return out;}
    std::deque<Json> takePlayerHits() {std::deque<Json> out;out.swap(playerHits);return out;}
    std::uint64_t playerEntity(std::uint16_t peer) const {
        if(role==Role::Host) {const auto* p=realm.find(peer);return p?p->entity:0;}
        for(const auto& p:world.value("players",Json::array())) if(p.at("peer")==peer)return p.at("entity").get<std::uint64_t>();
        return 0;
    }
    std::optional<Realm::NativeState> nativeState(std::uint16_t peer) const {
        if(!ready())return {};
        if(role==Role::Host) {const auto* p=realm.find(peer);return realm.stateReady(peer)?std::optional<Realm::NativeState>(p->state):std::nullopt;}
        if(role!=Role::Client||nowMs()-seenAt>500)return {};
        for(const auto& p:world.value("players",Json::array()))if(p.at("peer")==peer&&p.value("in_scene",false)&&p.value("native_bound",false)&&p.contains("native_state")) {
            const auto& s=p.at("native_state");Realm::NativeState state;
            state.position=s.at("position").get<std::array<double,3>>();state.hp=s.at("hp").get<double>();state.maxHp=s.at("max_hp").get<double>();
            state.alive=s.at("alive").get<bool>();state.observed=seenAt;return state;
        }
        return {};
    }
    bool publishAppearance(Net& net,const std::string& model,const Json& appearance) {
        if(!ready()||(role!=Role::Host&&role!=Role::Client)||characterTemplate(model).empty()||
           (!appearance.is_null()&&!validCharacterAppearance(appearance,model)))return false;
        const auto s=net.snapshot();const auto* p=find(s,s.localId);
        if(!fresh(p)||p->info.modelPath!=model||p->info.mapId!=world.value("map",std::string{})||
           p->info.levelId!=world.value("level",std::string{}))return false;
        if(lastAppearancePublish&&model==lastAppearanceModel&&appearance==lastAppearance&&nowMs()-lastAppearancePublish<1000)return true;
        if(role==Role::Host) {
            if(appearance.is_null())appearances.erase(s.localId);
            else appearances[s.localId]={model,appearance,nowMs()};
        } else if(!send(net,{{"kind","character_appearance"},{"epoch",epoch},{"model",model},{"appearance",appearance}},proto::kHostId))return false;
        lastAppearancePublish=nowMs();lastAppearanceModel=model;lastAppearance=appearance;return true;
    }
    Json requestedAppearance(std::uint16_t peer) const {
        if(!ready())return nullptr;
        if(role==Role::Host) {
            auto it=appearances.find(peer);const auto* player=realm.find(peer);
            if(it!=appearances.end()&&player&&player->inScene&&player->model==it->second.model&&
               nowMs()-it->second.seen<=3000)return it->second.profile;
        } else if(role==Role::Client)for(const auto& player:world.value("players",Json::array()))
            if(player.at("peer")==peer&&player.value("in_scene",false)&&player.contains("appearance"))return player.at("appearance");
        return nullptr;
    }
    Json appearanceStatus() const {
        auto out=Json::array();
        for(const auto& player:world.value("players",Json::array())) {
            const auto peer=player.at("peer").get<std::uint16_t>();
            if(auto profile=requestedAppearance(peer);!profile.is_null()) {
                auto applied=appliedAppearances.find(peer);
                const bool confirmed=applied!=appliedAppearances.end()&&applied->second.actor==playerEntity(peer)&&applied->second.epoch==epoch&&
                    applied->second.profile==profile&&nowMs()-applied->second.seen<2500;
                out.push_back({{"peer",peer},{"profile",profile},{"applied",confirmed}});
            }
        }
        return out;
    }
    void publishGrowth(Net& net,const std::string& model,const Json& growth) {
        if(!validCharacterGrowth(growth,model)||(role!=Role::Host&&role!=Role::Client)||nowMs()-lastGrowthPublish<1000)return;
        lastGrowthPublish=nowMs();
        if(role==Role::Host)growthRequests[net.snapshot().localId]=growth;
        else send(net,{{"kind","character_growth"},{"model",model},{"growth",growth}},proto::kHostId);
    }
    Json requestedGrowth(std::uint16_t peer) const {
        if(role==Role::Host) {auto it=growthRequests.find(peer);return it==growthRequests.end()?Json():it->second;}
        if(role==Role::Client&&ready())for(const auto& p:world.value("players",Json::array()))if(p.at("peer")==peer&&p.contains("growth"))return p.at("growth");
        return Json();
    }
    bool recordGrowth(std::uint16_t peer,std::uint64_t entity,std::uint64_t sceneEpoch,const Json& growth) {
        return role==Role::Host&&realm.recordGrowth(peer,entity,sceneEpoch,growth);
    }
    bool bindPlayer(std::uint16_t peer,const std::string& model,const std::map<std::string,Realm::SkillRule>& catalog) {
        if(role!=Role::Host||!ready())return false;
        const bool bound=realm.bind(peer,playerEntity(peer),epoch,model,catalog);
        if(bound)entries.erase(peer);return bound;
    }
    void unbindPlayer(std::uint16_t peer) {realm.unbind(peer);entries.erase(peer);appliedAppearances.erase(peer);commands.clear();}
    bool recordAppearance(std::uint16_t peer,std::uint64_t actor,std::uint64_t sceneEpoch,const Json& profile,bool nativeReady) {
        if(!nativeReady) {appliedAppearances.erase(peer);return false;}
        if(!ready()||!actor||actor!=playerEntity(peer)||sceneEpoch!=epoch||profile.is_null()||profile!=requestedAppearance(peer))return false;
        appliedAppearances[peer]={actor,sceneEpoch,nowMs(),profile};return true;
    }
    bool observePlayer(std::uint16_t peer,std::uint64_t actor,std::uint64_t sceneEpoch,Realm::NativeState state) {
        return role==Role::Host&&realm.observe(peer,actor,sceneEpoch,state);
    }
    bool ready() const {return !sceneResetPending&&epoch&&!world.empty()&&nowMs()-seenAt<3000;}
    bool protocolCompatible(std::uint16_t peer) const {
        auto it=compatibility.find(peer);
        return it!=compatibility.end()&&it->second.compatible&&nowMs()-it->second.seen<=3500;
    }
    void setAdapterVersion(std::string version) {
        if(version.size()>64)version.clear();if(version==adapterVersion)return;
        adapterVersion=std::move(version);compatibility.clear();lastHello=0;admitted=false;
        playerCasts.clear();castEvents.clear();hitClaimLedger.clear();
        for(auto& [id,entry]:entries)entry.active=false;
        commands.clear();hits.clear();playerHits.clear();enemyCasts.clear();enemyStates.clear();
    }
    Json protocolStatus() const {
        auto peers=Json::array();for(const auto& [id,entry]:compatibility)
            peers.push_back({{"peer",id},{"compatible",protocolCompatible(id)},{"build",entry.build},{"age_ms",nowMs()-entry.seen}});
        return {{"world_schema",coopWorldSchema},{"contract_revision",coopContractRevision},{"contract",coopContractName},
            {"build",EFLINK_BUILD_ID},{"game_version",adapterVersion},{"peers",peers},{"compatible",role==Role::Host?!adapterVersion.empty():protocolCompatible(proto::kHostId)}};
    }
    void setAuthority(AuthorityFacts facts) {
        authority=facts;if(!facts.complete()) {for(auto& [id,e]:entries)e.active=false;commands.clear();hits.clear();}
    }
    void setLocalSceneReady(bool ready) {
        if(localSceneReady==ready)return;
        localSceneReady=ready;
        if(!ready) {
            admitted=false;replica={};lastEntryAck=lastEntryReport=0;
            for(auto& [id,entry]:entries)entry.active=false;
            commands.clear();hits.clear();playerHits.clear();enemyCasts.clear();enemyStates.clear();castEvents.clear();
            resourceBaseline=Json::array();resourceSeen=0;
        }
    }
    bool localSceneOperational() const {return localSceneReady;}
    void invalidateNativeScene() {
        setLocalSceneReady(false);
        if(role==Role::Host&&!world.empty())sceneResetPending=true;
    }
    void reportEntry(Net& net,ReplicaFacts facts) {
        replica=localSceneReady&&followHost&&combat?facts:ReplicaFacts{};
        facts=replica;
        if(!facts.complete()) admitted=false;
        if(role!=Role::Client||!ready()||nowMs()-lastEntryReport<500) return;
        lastEntryReport=nowMs();
        const auto actor=playerEntity(net.snapshot().localId);if(!actor)return;
        send(net,{{"kind","entry_prepare"},{"epoch",epoch},{"revision",revision},{"token",entryToken},
            {"map",world.at("map")},{"level",world.at("level")},{"actor",actor},{"replica",facts.json()}},proto::kHostId);
    }
    bool canAct(std::uint16_t id) const {
        if(!ready()||adapterVersion.empty()||!localSceneReady||!followHost||!combat) return false;
        if(role==Role::Client) return !baselineMissing&&protocolCompatible(proto::kHostId)&&admitted&&replica.complete()&&lastEntryAck&&nowMs()-lastEntryAck<3000;
        if(role!=Role::Host||publishBlocked||!authority.complete()||!realm.bound(id)) return false;
        if(id==proto::kHostId) return true;
        auto it=entries.find(id);return protocolCompatible(id)&&it!=entries.end()&&it->second.active&&it->second.epoch==epoch&&
            it->second.actor==playerEntity(id)&&nowMs()-it->second.seen<3000;
    }
    bool canObservePlayer(std::uint16_t id) const {
        if(!ready()||!localSceneReady||adapterVersion.empty()||!followHost||!combat)return false;
        if(role==Role::Host) {
            if(!realm.bound(id))return false;
            if(id==proto::kHostId)return true;
            auto it=entries.find(id);return protocolCompatible(id)&&it!=entries.end()&&it->second.epoch==epoch&&
                it->second.actor==playerEntity(id)&&it->second.facts.complete()&&nowMs()-it->second.seen<3000;
        }
        if(role!=Role::Client||baselineMissing||!protocolCompatible(proto::kHostId)||!replica.complete())return false;
        for(const auto& player:world.value("players",Json::array()))if(player.at("peer")==id)
            return player.value("native_bound",false)&&player.value("in_scene",false);
        return false;
    }
    Json entryStatus() const {
        Json members=Json::array();for(const auto& [id,e]:entries) members.push_back({{"id",id},{"epoch",e.epoch},
            {"active",canAct(id)},{"replica",e.facts.json()}});
        const bool active=role==Role::Host?canAct(proto::kHostId):role==Role::Client&&canAct(0);
        std::string phase=role==Role::Offline?"offline":!ready()?"awaiting_snapshot":active?"active":
            role==Role::Host?"host_adapter_incomplete":!replica.mapLoaded?"loading_host_scene":!replica.entitiesReady?"preparing_entities":
            !replica.worldIsolated||!replica.logicalPlayers?"guest_adapter_incomplete":"waiting_host_commit";
        if(role==Role::Client&&!protocolCompatible(proto::kHostId))phase=compatibility.contains(proto::kHostId)?"incompatible_protocol":"negotiating_protocol";
        else if(role==Role::Client&&baselineMissing)phase="awaiting_full_baseline";
        else if(role==Role::Host&&publishBlocked)phase="host_transport_blocked";
        else if(role!=Role::Offline&&!localSceneReady)phase="local_scene_loading";
        else if(role!=Role::Offline&&(!followHost||!combat))phase="world_sync_disabled";
        return {{"phase",phase},{"active",active},{"local_scene_ready",localSceneReady},{"authority",authority.json()},{"replica",replica.json()},
            {"official_instance_shared",false},{"account_state_authority","official_server"},{"stage1_verified",false},
            {"blockers",role==Role::Host?authority.blockers():entryBlockers},{"members",members},
            {"players",role==Role::Host?realm.json():world.value("players",Json::array())}};
    }
    const Json& snapshot() const {return world;}
    std::uint64_t snapshotTime() const {return seenAt;}
    std::uint64_t worldRevision() const {return revision;}
    std::uint64_t generation() const {return epoch;}
    Json entityReference(const std::string& key) const {
        if(!ready())return nullptr;
        if(role==Role::Host)return entityLedger.reference(key);
        for(const auto& e:world.value("entities",Json::array()))if(e.at("key")==key&&e.value("alive",true)&&e.at("hp").get<double>()>0)
            return {{"life",e.at("life")},{"lease",e.at("lease")},{"owner",e.at("owner")}};
        return nullptr;
    }
    bool entityMatches(const std::string& key,std::uint64_t life,std::uint64_t lease=0,std::uint16_t owner=0) const {
        if(!ready())return false;
        if(role==Role::Host)return entityLedger.matches(key,life,lease,owner);
        for(const auto& e:world.value("entities",Json::array()))if(e.at("key")==key&&e.value("alive",true)&&e.at("hp").get<double>()>0)
            return e.at("life")==life&&(!lease||e.at("lease")==lease)&&(!owner||e.at("owner")==owner);
        return false;
    }
    bool assignEnemyOwner(const std::string& key,std::uint16_t owner) {
        return role==Role::Host&&canAct(owner)&&entityLedger.assign(key,owner);
    }
    Json takeTeleport() {auto out=teleport;teleport=Json::object();return out;}
    std::map<std::uint16_t,Json> takeActorTeleports() {std::map<std::uint16_t,Json> out;out.swap(actorTeleports);return out;}
    std::deque<Json> takeCommands() {std::deque<Json> out;out.swap(commands);return out;}
    void failure(std::string value) {error=std::move(value);}
    Json status() const {const auto presentation=appearanceStatus();return {{"schema",coopWorldSchema},{"protocol",protocolStatus()},{"epoch",epoch},{"revision",revision},{"ready",ready()},
        {"map",world.value("map",std::string{})},{"level",world.value("level",std::string{})},
        {"entities",world.value("entities",Json::array()).size()},{"follow_host",followHost},{"combat",combat},
        {"appearance_profiles",presentation.size()},{"appearances",presentation},{"appearances_received",appearancesReceived},
        {"transport",{{"baselines_sent",baselinesSent},{"deltas_sent",deltasSent},{"world_bytes",wireBytes},{"baseline_missing",baselineMissing},{"publish_blocked",publishBlocked},{"congestion_skipped",congestionSkipped}}},
        {"presentation",{{"received",presentationReceived},{"raw_bytes",presentationRawBytes},{"wire_bytes",presentationWireBytes},{"compressed_frames",presentationPacked},{"congestion_skipped",presentationSkipped},{"coalesced",presentationCoalesced.load()},{"peers",presentations.size()},
            {"ages",[&]{Json ages=Json::array();for(const auto& [peer,p]:presentations)ages.push_back({{"peer",peer},{"age_ms",nowMs()-p.seen}});return ages;}()},{"native_skill_replay",false}}},
        {"player_casts",{{"accepted",castEventsAccepted},{"active",playerCasts.active(nowMs())},{"replay_enabled",false}}},
        {"hit_claims",{{"accepted",hitsAccepted},{"legacy_numeric_rejected",legacyHitsRejected},{"native_verification_required",true}}},
        {"resources",{{"observed",resources().size()},{"revision",resourceRevision},{"age_ms",resourceSeen?nowMs()-resourceSeen:0},
            {"operation_adapter_verified",false},{"replica_adapter_verified",false}}},
        {"age_ms",seenAt?nowMs()-seenAt:0},{"snapshots_received",received},{"skill_intents_accepted",attacks},{"hits_accepted",hitsAccepted},{"rejected",rejected},{"entry",entryStatus()},{"error",error}};}
};
}
