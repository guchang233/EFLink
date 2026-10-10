#pragma once
// Host-owned world state. This layer contains no Unity pointers and can also be used by
// external world adapters. All state changes occur on the game tick; network callbacks
// only put bounded, copied packets in the inbox.
#include "link_net.h"
#include "link_entry.h"
#include "link_realm.h"
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
inline bool validWorld(const Json& w) {
    try {
        if(!w.is_object()||!w.at("map").is_string()||w.at("map").get<std::string>().size()>64||
           !w.at("level").is_string()||w.at("level").get<std::string>().size()>64||!validVector(w.at("position"))||
           !w.at("level_num").is_number_integer()||w.at("level_num").get<int>()<0) return false;
        const auto& entities=w.at("entities");if(!entities.is_array()||entities.size()>512) return false;
        std::set<std::string> keys;
        for(const auto& e:entities) {
            auto key=e.at("key").get<std::string>();if(key.empty()||key.size()>128||!keys.insert(key).second||!validVector(e.at("position"))) return false;
            const double hp=e.at("hp").get<double>(),max=e.at("max_hp").get<double>();
            if(!std::isfinite(hp)||!std::isfinite(max)||hp<0||max<=0||max>1e12||hp>max*2||
                e.value("template",std::string{}).size()>256) return false;
            if(e.contains("enemy_level")&&(!e.at("enemy_level").is_number_integer()||e.at("enemy_level").get<int>()<1||e.at("enemy_level").get<int>()>1000))return false;
            if(e.contains("rotation")) {const auto& q=e["rotation"];if(!q.is_array()||q.size()!=4) return false;
                double length=0;for(const auto& v:q){if(!v.is_number()||!std::isfinite(v.get<double>()))return false;length+=v.get<double>()*v.get<double>();}
                if(length<.5||length>1.5) return false;}
        }
        return true;
    } catch(...) {return false;}
}
class Session {
    struct Packet {std::uint16_t from;Json body;std::size_t size;};
    std::mutex inboxMutex;std::deque<Packet> inbox;std::size_t inboxBytes=0;
    std::uint32_t room=0;Role role=Role::Offline;
    std::uint64_t epoch=0,revision=0,seenAt=0,lastPublish=0,commandSequence=0;
    std::map<std::uint16_t,std::uint64_t> sequences;
    std::map<std::uint16_t,std::pair<std::uint64_t,unsigned>> attackBudgets;
    std::map<std::uint16_t,Json> locations;std::uint64_t lastLocation=0;
    struct MemberEntry {std::uint64_t epoch=0,token=0,seen=0;bool active=false;ReplicaFacts facts;};
    std::map<std::uint16_t,MemberEntry> entries;
    AuthorityFacts authority;ReplicaFacts replica;Realm realm;
    std::uint64_t entryToken=0,lastEntryReport=0,lastEntryAck=0;
    bool admitted=false;
    Json entryBlockers=Json::array();
    Json world=Json::object();std::deque<Json> commands;Json teleport=Json::object();
    std::map<std::uint16_t,Json> actorTeleports;
    std::map<std::uint16_t,Json> growthRequests;
    // Hit results. Like an official co-op world, each player's own client resolves
    // its attacks; the host bounds them and owns the enemy's HP and death.
    std::deque<Json> hits,playerHits,enemyCasts,enemyStates;std::map<std::uint16_t,std::uint64_t> hitSequences;
    std::map<std::uint16_t,std::pair<std::uint64_t,unsigned>> hitBudgets;std::uint64_t hitSequence=0,hitsAccepted=0;
    std::uint64_t lastGrowthPublish=0;
    std::uint64_t rejected=0,received=0,attacks=0;std::string error;
    bool send(Net& net,Json body,std::uint16_t to=proto::kEveryone) {
        body["schema"]=2;body["room"]=room;
        const auto text=body.dump();if(text.size()>proto::kMaxReliableData) {error="世界快照超过网络上限";return false;}
        return net.sendData(worldChannel,to,text.data(),text.size(),true);
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
        else if(auto it=locations.find(target);it!=locations.end()&&it->second.value("map",std::string{})==p->info.mapId&&it->second.value("level",std::string{})==p->info.levelId) request["level_num"]=it->second.value("level_num",0);
        if(actor==s.localId) teleport=request;
        else if(send(net,request,actor)&&role==Role::Host&&request.at("map")==world.value("map",std::string{})&&request.at("level")==world.value("level",std::string{}))
            actorTeleports[actor]=request;
    }
public:
    bool followHost=true,combat=true;
    void receive(std::uint16_t from,const void* data,std::size_t size) {
        if(!data||!size||size>proto::kMaxReliableData) return;
        auto parsed=Json::parse(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+size,nullptr,false);
        if(parsed.is_discarded()||!parsed.is_object()) return;
        std::lock_guard lock(inboxMutex);
        if(inbox.size()>=128||inboxBytes+size>4u*1024*1024) return;
        inbox.push_back({from,std::move(parsed),size});inboxBytes+=size;
    }
    void update(Net& net) {
        const auto s=net.snapshot();
        if(s.roomId!=room||s.role!=role) {
            room=s.roomId;role=s.role;epoch=revision=seenAt=lastPublish=commandSequence=0;world=Json::object();
            hits.clear();playerHits.clear();enemyCasts.clear();enemyStates.clear();hitSequences.clear();hitBudgets.clear();hitSequence=0;
            sequences.clear();attackBudgets.clear();locations.clear();lastLocation=0;commands.clear();teleport=Json::object();actorTeleports.clear();error.clear();
            entries.clear();replica={};entryToken=lastEntryReport=lastEntryAck=0;admitted=false;entryBlockers=Json::array();
            realm.reset(room);
            growthRequests.clear();lastGrowthPublish=0;
        }
        if(role==Role::Host&&ready()) realm.reconcile(s,epoch,world.value("map",std::string{}),world.value("level",std::string{}));
        for(auto it=entries.begin();it!=entries.end();) {if(!find(s,it->first)) it=entries.erase(it);else ++it;}
        for(auto it=growthRequests.begin();it!=growthRequests.end();) {
            const auto* p=find(s,it->first);
            if(!p||!validCharacterGrowth(it->second,p->info.modelPath))it=growthRequests.erase(it);else ++it;
        }
        std::deque<Packet> pending;
        {std::lock_guard lock(inboxMutex);pending.swap(inbox);inboxBytes=0;}
        for(const auto& packet:pending) {
            try {
                const auto& b=packet.body;
                if(!room||b.at("schema")!=2||b.at("room").get<std::uint32_t>()!=room||!find(s,packet.from)) {++rejected;continue;}
                const auto kind=b.at("kind").get<std::string>();
                if(kind=="snapshot") {
                    const auto e=b.at("epoch").get<std::uint64_t>(),r=b.at("revision").get<std::uint64_t>();
                    if(role!=Role::Client||packet.from!=proto::kHostId||!e||e<epoch||(e==epoch&&r<=revision)||!validWorld(b.at("world"))||
                       !validPlayers(b.at("world").at("players"),e)) {++rejected;continue;}
                    if(e!=epoch) {teleport=Json::object();commands.clear();replica={};admitted=false;playerHits.clear();enemyCasts.clear();
                        entryToken=nowMs()^(std::uint64_t(room)<<32)^e; if(!entryToken)entryToken=1;
                        lastEntryReport=lastEntryAck=0;entryBlockers=Json::array();}
                    epoch=e;revision=r;world=b["world"];seenAt=nowMs();++received;
                } else if(kind=="entry_prepare"&&role==Role::Host) {
                    const auto e=b.at("epoch").get<std::uint64_t>(),token=b.at("token").get<std::uint64_t>();
                    const auto r=b.at("revision").get<std::uint64_t>();
                    if(!ready()||e!=epoch||!token||!r||r>revision||packet.from==proto::kHostId||
                        b.at("map")!=world.at("map")||b.at("level")!=world.at("level")) {++rejected;continue;}
                    auto facts=ReplicaFacts::parse(b.at("replica"));
                    auto blocks=authority.blockers();for(const auto& x:facts.blockers()) blocks.push_back(x);
                    const auto* actor=realm.find(packet.from);
                    if(!actor||!realm.bound(packet.from)) blocks.push_back("host_player_entity");
                    if(!actor||b.at("actor").get<std::uint64_t>()!=actor->entity) {++rejected;continue;}
                    const bool active=authority.complete()&&facts.complete()&&realm.bound(packet.from);
                    entries[packet.from]={epoch,token,nowMs(),active,facts};
                    send(net,{{"kind","entry_result"},{"epoch",epoch},{"token",token},{"actor",actor->entity},{"active",active},{"blockers",blocks}},packet.from);
                } else if(kind=="entry_result"&&role==Role::Client&&packet.from==proto::kHostId) {
                    const auto& blocks=b.at("blockers");
                    if(!ready()||b.at("epoch").get<std::uint64_t>()!=epoch||b.at("token").get<std::uint64_t>()!=entryToken||
                       !entryToken||b.at("actor").get<std::uint64_t>()!=playerEntity(s.localId)||!blocks.is_array()||blocks.size()>16) {++rejected;continue;}
                    bool valid=true;for(const auto& x:blocks) if(!x.is_string()||x.get<std::string>().size()>128) valid=false;
                    if(!valid) {++rejected;continue;}
                    admitted=b.at("active").get<bool>()&&blocks.empty()&&replica.complete();entryBlockers=blocks;lastEntryAck=nowMs();
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
                    teleport=b;teleport["issued"]=nowMs();
                } else if(kind=="teleport_error"&&role==Role::Client&&packet.from==proto::kHostId) error=b.at("message").get<std::string>().substr(0,512);
                else if(kind=="skill_intent"&&role==Role::Host&&combat) {
                    const auto seq=b.at("seq").get<std::uint64_t>();const auto now=nowMs();
                    const auto skill=b.at("skill").get<std::string>(),key=b.at("key").get<std::string>();
                    const auto* p=find(s,packet.from);
                    if(!canAct(packet.from)||!seq||seq<=sequences[packet.from]||b.at("epoch").get<std::uint64_t>()!=epoch||!fresh(p)||
                       b.at("actor").get<std::uint64_t>()!=playerEntity(packet.from)||b.contains("amount")||b.contains("damage")||
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
                        {"actor",playerEntity(packet.from)},{"key",key},{"skill",skill}});++attacks;
                } else if(kind=="hit"&&role==Role::Host&&combat) {
                    const auto seq=b.at("seq").get<std::uint64_t>();const auto now=nowMs();
                    const auto key=b.at("key").get<std::string>();const double amount=b.at("amount").get<double>();
                    const auto* p=find(s,packet.from);
                    if(!canAct(packet.from)||!seq||seq<=hitSequences[packet.from]||b.at("epoch").get<std::uint64_t>()!=epoch||!fresh(p)||
                       key.empty()||key.size()>128||!std::isfinite(amount)||amount<=0||hits.size()>=256||
                       p->info.mapId!=world.value("map",std::string{})||p->info.levelId!=world.value("level",std::string{})) {++rejected;continue;}
                    hitSequences[packet.from]=seq;
                    auto& budget=hitBudgets[packet.from];if(now-budget.first>=1000) budget={now,0};
                    if(budget.second>=40) {++rejected;continue;}
                    bool target=false;
                    for(const auto& entity:world.value("entities",Json::array())) if(entity.at("key")==key) {
                        // One hit can never exceed the target's whole native health.
                        target=entity.at("hp").get<double>()>0&&entity.value("alive",true)&&amount<=entity.at("max_hp").get<double>();break;
                    }
                    if(!target) {++rejected;continue;}
                    ++budget.second;++hitsAccepted;
                    hits.push_back({{"from",packet.from},{"epoch",epoch},{"key",key},{"amount",amount}});
                } else if(kind=="player_hit"&&role==Role::Client&&packet.from==proto::kHostId) {
                    const double amount=b.at("amount").get<double>();
                    if(b.at("epoch").get<std::uint64_t>()!=epoch||!std::isfinite(amount)||amount<=0||amount>1e9||playerHits.size()>=64) {++rejected;continue;}
                    playerHits.push_back({{"amount",amount},{"key",b.value("key",std::string{}).substr(0,128)}});
                } else if(kind=="player_hit"&&role==Role::Host&&canAct(packet.from)) {
                    // An enemy simulated on that guest hit another player's stand-in
                    // there. The host applies it to its own operator or relays it.
                    const double amount=b.at("amount").get<double>();const auto target=b.at("target").get<std::uint16_t>();
                    const auto key=b.value("key",std::string{}).substr(0,128);
                    if(b.at("epoch").get<std::uint64_t>()!=epoch||!std::isfinite(amount)||amount<=0||amount>1e9||target==packet.from||!find(s,target)) {++rejected;continue;}
                    auto& budget=hitBudgets[packet.from];if(nowMs()-budget.first>=1000) budget={nowMs(),0};
                    if(budget.second>=40) {++rejected;continue;}
                    ++budget.second;
                    if(target==s.localId) {if(playerHits.size()<64)playerHits.push_back({{"amount",amount},{"key",key}});}
                    else send(net,{{"kind","player_hit"},{"epoch",epoch},{"amount",amount},{"key",key}},target);
                } else if(kind=="enemy_cast"&&(role==Role::Client?packet.from==proto::kHostId:role==Role::Host&&canAct(packet.from))) {
                    // From the host: any room enemy. From a guest: an enemy that guest
                    // owns (the game adapter checks ownership before replaying it).
                    const auto key=b.at("key").get<std::string>(),skill=b.at("skill").get<std::string>();
                    if(b.at("epoch").get<std::uint64_t>()!=epoch||key.empty()||key.size()>128||skill.empty()||skill.size()>128||enemyCasts.size()>=64) {++rejected;continue;}
                    enemyCasts.push_back({{"key",key},{"skill",skill},{"target",b.at("target").get<std::uint16_t>()},{"from",packet.from}});
                } else if(kind=="enemy_state"&&role==Role::Host&&canAct(packet.from)) {
                    // Movement of enemies whose AI runs on that guest (ownership is
                    // assigned and checked by the host's game adapter).
                    const auto& states=b.at("states");
                    if(b.at("epoch").get<std::uint64_t>()!=epoch||!states.is_array()||states.size()>64||enemyStates.size()>=256) {++rejected;continue;}
                    bool valid=true;
                    for(const auto& item:states) {
                        if(!item.at("key").is_string()||item.at("key").get<std::string>().size()>128||!validVector(item.at("position"))) {valid=false;break;}
                        const auto& q=item.at("rotation");if(!q.is_array()||q.size()!=4) {valid=false;break;}
                        for(const auto& v:q)if(!v.is_number()||!std::isfinite(v.get<double>())) valid=false;
                    }
                    if(!valid) {++rejected;continue;}
                    enemyStates.push_back({{"from",packet.from},{"states",states}});
                } else ++rejected;
            } catch(...) {++rejected;}
        }
    }
    bool publish(Net& net,Json value,bool force=false) {
        if(role!=Role::Host||!validWorld(value)) return false;
        if(world.empty()||world.value("map",std::string{})!=value.at("map").get<std::string>()||world.value("level",std::string{})!=value.at("level").get<std::string>()) {
            ++epoch;revision=0;commands.clear();hits.clear();teleport=Json::object();actorTeleports.clear();entries.clear();
        }
        realm.reconcile(net.snapshot(),epoch,value.at("map").get<std::string>(),value.at("level").get<std::string>());
        value["players"]=realm.json();
        world=std::move(value);seenAt=nowMs();
        if(!force&&nowMs()-lastPublish<50) return true;
        lastPublish=nowMs();++revision;return send(net,{{"kind","snapshot"},{"epoch",epoch},{"revision",revision},{"world",world}});
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
        return send(net,{{"kind","skill_intent"},{"epoch",epoch},{"seq",++commandSequence},{"actor",playerEntity(net.snapshot().localId)},
            {"key",key},{"skill",skill}},proto::kHostId);
    }
    bool submitHit(Net& net,const std::string& key,double amount) {
        if(role!=Role::Client||!combat||!canAct(net.snapshot().localId)||!std::isfinite(amount)||amount<=0) return false;
        return send(net,{{"kind","hit"},{"epoch",epoch},{"seq",++hitSequence},{"key",key},{"amount",amount}},proto::kHostId);
    }
    bool sendPlayerHit(Net& net,std::uint16_t peer,double amount,const std::string& key) {
        if(!ready()||!std::isfinite(amount)||amount<=0) return false;
        if(role==Role::Host) return peer!=proto::kHostId&&send(net,{{"kind","player_hit"},{"epoch",epoch},{"amount",amount},{"key",key}},peer);
        // A guest forwards through the host, which owns the room's rules.
        return role==Role::Client&&canAct(net.snapshot().localId)&&peer!=net.snapshot().localId&&
            send(net,{{"kind","player_hit"},{"epoch",epoch},{"amount",amount},{"key",key},{"target",peer}},proto::kHostId);
    }
    // Presentation: a host room enemy began a native skill against a player.
    bool sendEnemyCast(Net& net,const std::string& key,const std::string& skill,std::uint16_t target) {
        if(role!=Role::Host||!ready()||key.empty()||skill.empty()||skill.size()>128) return false;
        return send(net,{{"kind","enemy_cast"},{"epoch",epoch},{"key",key},{"skill",skill},{"target",target}});
    }
    std::deque<Json> takeEnemyCasts() {std::deque<Json> out;out.swap(enemyCasts);return out;}
    // Guest: report the room enemies whose AI this player runs.
    bool sendEnemyStates(Net& net,const Json& states) {
        if(role!=Role::Client||!canAct(net.snapshot().localId)||!states.is_array()||states.empty()||states.size()>64) return false;
        return send(net,{{"kind","enemy_state"},{"epoch",epoch},{"states",states}},proto::kHostId);
    }
    // Guest: an enemy it owns began a native skill; the host replays and relays it.
    bool reportEnemyCast(Net& net,const std::string& key,const std::string& skill,std::uint16_t target) {
        if(role!=Role::Client||!canAct(net.snapshot().localId)||key.empty()||skill.empty()||skill.size()>128) return false;
        return send(net,{{"kind","enemy_cast"},{"epoch",epoch},{"key",key},{"skill",skill},{"target",target}},proto::kHostId);
    }
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
    void unbindPlayer(std::uint16_t peer) {realm.unbind(peer);entries.erase(peer);commands.clear();}
    bool observePlayer(std::uint16_t peer,std::uint64_t actor,std::uint64_t sceneEpoch,Realm::NativeState state) {
        return role==Role::Host&&realm.observe(peer,actor,sceneEpoch,state);
    }
    bool ready() const {return epoch&&!world.empty()&&nowMs()-seenAt<3000;}
    void setAuthority(AuthorityFacts facts) {
        authority=facts;if(!facts.complete()) {for(auto& [id,e]:entries)e.active=false;commands.clear();hits.clear();}
    }
    void reportEntry(Net& net,ReplicaFacts facts) {
        replica=facts;
        if(!facts.complete()) admitted=false;
        if(role!=Role::Client||!ready()||nowMs()-lastEntryReport<500) return;
        lastEntryReport=nowMs();
        const auto actor=playerEntity(net.snapshot().localId);if(!actor)return;
        send(net,{{"kind","entry_prepare"},{"epoch",epoch},{"revision",revision},{"token",entryToken},
            {"map",world.at("map")},{"level",world.at("level")},{"actor",actor},{"replica",facts.json()}},proto::kHostId);
    }
    bool canAct(std::uint16_t id) const {
        if(!ready()) return false;
        if(role==Role::Client) return admitted&&replica.complete()&&lastEntryAck&&nowMs()-lastEntryAck<3000;
        if(role!=Role::Host||!authority.complete()||!realm.bound(id)) return false;
        if(id==proto::kHostId) return true;
        auto it=entries.find(id);return it!=entries.end()&&it->second.active&&it->second.epoch==epoch&&nowMs()-it->second.seen<3000;
    }
    Json entryStatus() const {
        Json members=Json::array();for(const auto& [id,e]:entries) members.push_back({{"id",id},{"epoch",e.epoch},
            {"active",canAct(id)},{"replica",e.facts.json()}});
        const bool active=role==Role::Host?canAct(proto::kHostId):role==Role::Client&&canAct(0);
        std::string phase=role==Role::Offline?"offline":!ready()?"awaiting_snapshot":active?"active":
            role==Role::Host?"host_adapter_incomplete":!replica.mapLoaded?"loading_host_scene":!replica.entitiesReady?"preparing_entities":
            !replica.worldIsolated||!replica.logicalPlayers?"guest_adapter_incomplete":"waiting_host_commit";
        return {{"phase",phase},{"active",active},{"authority",authority.json()},{"replica",replica.json()},
            {"blockers",role==Role::Host?authority.blockers():entryBlockers},{"members",members},
            {"players",role==Role::Host?realm.json():world.value("players",Json::array())}};
    }
    const Json& snapshot() const {return world;}
    std::uint64_t snapshotTime() const {return seenAt;}
    std::uint64_t worldRevision() const {return revision;}
    std::uint64_t generation() const {return epoch;}
    Json takeTeleport() {auto out=teleport;teleport=Json::object();return out;}
    std::map<std::uint16_t,Json> takeActorTeleports() {std::map<std::uint16_t,Json> out;out.swap(actorTeleports);return out;}
    std::deque<Json> takeCommands() {std::deque<Json> out;out.swap(commands);return out;}
    void failure(std::string value) {error=std::move(value);}
    Json status() const {return {{"schema",2},{"epoch",epoch},{"revision",revision},{"ready",ready()},
        {"map",world.value("map",std::string{})},{"level",world.value("level",std::string{})},
        {"entities",world.value("entities",Json::array()).size()},{"follow_host",followHost},{"combat",combat},
        {"age_ms",seenAt?nowMs()-seenAt:0},{"snapshots_received",received},{"skill_intents_accepted",attacks},{"hits_accepted",hitsAccepted},{"rejected",rejected},{"entry",entryStatus()},{"error",error}};}
};
}
