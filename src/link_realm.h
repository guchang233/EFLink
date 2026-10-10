#pragma once
#include "link_net.h"
#include "link_profile.h"
#include "nlohmann/json.hpp"
#include <array>

namespace eflink {
// The listen server owns these identities. Reserving an identity is not spawning
// a native character: only the game adapter may bind an actual ability actor.
class Realm {
public:
    struct SkillRule {std::uint32_t cooldownMs=0;double range=0;};
    struct NativeState {
        std::array<double,3> position{};
        double hp=0,maxHp=0;bool alive=false;
        std::uint64_t observed=0;
    };
    struct Player {
        std::uint16_t peer=0;std::uint64_t entity=0,epoch=0;
        std::string name,model;bool nativeBound=false,inScene=false;
        std::map<std::string,SkillRule> skills;
        std::map<std::string,std::uint64_t> lastCast;
        NativeState state;
        nlohmann::json growth;
    };
private:
    std::uint32_t room=0;std::uint64_t epoch=0,next=0;
    std::map<std::uint16_t,Player> players;
public:
    void reset(std::uint32_t id=0) {room=id;epoch=next=0;players.clear();}
    void reconcile(const Snapshot& transport,std::uint64_t sceneEpoch,const std::string& map,const std::string& level) {
        if(transport.role!=Role::Host) return;
        if(transport.roomId!=room) reset(transport.roomId);
        if(epoch!=sceneEpoch) {epoch=sceneEpoch;players.clear();}
        std::set<std::uint16_t> connected;
        for(const auto& peer:transport.peers) {
            connected.insert(peer.id);
            auto [it,inserted]=players.try_emplace(peer.id);
            auto& player=it->second;
            if(inserted) {
                if(++next>65535) {players.erase(it);continue;} // never reuse an ID in this room
                player.peer=peer.id;player.epoch=epoch;
                player.entity=0xEF10000000000000ull|(std::uint64_t(room)<<16)|next;
            }
            // A new operator needs its own stats/skills. A mesh change cannot retain
            // the previous operator's combat binding or cooldown catalog.
            if(player.model!=peer.info.modelPath) {
                player.model=peer.info.modelPath;player.nativeBound=false;player.skills.clear();player.lastCast.clear();player.state={};player.growth=nullptr;
            }
            player.name=peer.info.name;
            player.inScene=peer.hasInfo&&peer.info.mapId==map&&peer.info.levelId==level&&
                !peer.states.empty()&&(peer.states.back().state.flags&proto::StateValid)&&
                peer.states.back().state.mapHash==proto::fnv32(map)&&nowMs()-peer.states.back().receivedMs<3000;
            if(!player.inScene) {player.nativeBound=false;player.skills.clear();player.lastCast.clear();player.state={};player.growth=nullptr;}
        }
        for(auto it=players.begin();it!=players.end();) {
            if(!connected.contains(it->first)) it=players.erase(it);else ++it;
        }
    }
    const Player* find(std::uint16_t peer) const {
        auto it=players.find(peer);return it==players.end()?nullptr:&it->second;
    }
    bool bind(std::uint16_t peer,std::uint64_t entity,std::uint64_t sceneEpoch,
              const std::string& model,const std::map<std::string,SkillRule>& catalog) {
        auto it=players.find(peer);
        if(it==players.end()||it->second.entity!=entity||sceneEpoch!=epoch||
           it->second.model!=model||!it->second.inScene||catalog.empty()||catalog.size()>64) return false;
        for(const auto& [id,rule]:catalog) if(id.empty()||id.size()>128||rule.cooldownMs>3600000||
            !std::isfinite(rule.range)||rule.range<=0||rule.range>120) return false;
        auto& player=it->second;
        if(player.nativeBound) return false; // do not reset cooldowns through a repeated bind
        player.skills=catalog;player.nativeBound=true;return true;
    }
    void unbind(std::uint16_t peer) {
        auto it=players.find(peer);if(it==players.end())return;
        it->second.nativeBound=false;it->second.skills.clear();it->second.lastCast.clear();it->second.state={};it->second.growth=nullptr;
    }
    bool observe(std::uint16_t peer,std::uint64_t entity,std::uint64_t sceneEpoch,NativeState state) {
        auto it=players.find(peer);
        if(it==players.end()||!bound(peer)||it->second.entity!=entity||sceneEpoch!=epoch||
           !std::isfinite(state.hp)||!std::isfinite(state.maxHp)||state.hp<0||state.maxHp<=0||state.maxHp>1e12||state.hp>state.maxHp*2)return false;
        for(double x:state.position)if(!std::isfinite(x)||std::abs(x)>1000000)return false;
        state.observed=nowMs();it->second.state=state;return true;
    }
    bool recordGrowth(std::uint16_t peer,std::uint64_t entity,std::uint64_t sceneEpoch,const nlohmann::json& growth) {
        auto it=players.find(peer);
        if(it==players.end()||!bound(peer)||it->second.entity!=entity||sceneEpoch!=epoch||!validCharacterGrowth(growth,it->second.model))return false;
        it->second.growth=growth;return true;
    }
    bool stateReady(std::uint16_t peer) const {
        const auto* player=find(peer);return bound(peer)&&player->state.observed&&nowMs()-player->state.observed<=500;
    }
    bool bound(std::uint16_t peer) const {const auto* p=find(peer);return p&&p->nativeBound&&p->inScene&&p->epoch==epoch;}
    bool allBound() const {
        if(players.empty())return false;
        for(const auto& [peer,p]:players) if(!bound(peer))return false;
        return true;
    }
    bool acceptSkill(std::uint16_t peer,const std::string& skill,double distanceSquared,std::uint64_t now) {
        if(!stateReady(peer)||!players.at(peer).state.alive||!std::isfinite(distanceSquared)||distanceSquared<0)return false;
        auto& player=players.at(peer);auto it=player.skills.find(skill);if(it==player.skills.end())return false;
        if(distanceSquared>it->second.range*it->second.range)return false;
        auto last=player.lastCast.find(skill);
        if(last!=player.lastCast.end()&&(now<last->second||now-last->second<it->second.cooldownMs))return false;
        // Reserve once at validation. Native rejection cannot grant a free retry flood.
        player.lastCast[skill]=now;return true;
    }
    nlohmann::json json() const {
        auto out=nlohmann::json::array();
        for(const auto& [peer,p]:players) {
            nlohmann::json player={{"peer",peer},{"entity",p.entity},{"epoch",p.epoch},
                {"name",p.name},{"model",p.model},{"in_scene",p.inScene},{"native_bound",p.nativeBound}};
            if(stateReady(peer))player["native_state"]={{"position",p.state.position},{"hp",p.state.hp},{"max_hp",p.state.maxHp},{"alive",p.state.alive}};
            if(bound(peer)&&!p.growth.is_null())player["growth"]=p.growth;
            out.push_back(std::move(player));
        }
        return out;
    }
};
inline bool validPlayers(const nlohmann::json& list,std::uint64_t epoch) {
    try {
        if(!list.is_array()||list.size()>proto::kMaxPlayers)return false;
        std::set<std::uint16_t> peers;std::set<std::uint64_t> entities;
        for(const auto& p:list) {
            if(!p.at("peer").is_number_unsigned()||!p.at("entity").is_number_unsigned()||
                !p.at("epoch").is_number_unsigned())return false;
            const auto peer=p.at("peer").get<std::uint64_t>(),entity=p.at("entity").get<std::uint64_t>();
            if(!peer||peer>=proto::kEveryone||!entity||p.at("epoch").get<std::uint64_t>()!=epoch||
                !peers.insert(std::uint16_t(peer)).second||!entities.insert(entity).second||
                p.at("name").get<std::string>().size()>128||p.at("model").get<std::string>().size()>512||
                !p.at("in_scene").is_boolean()||!p.at("native_bound").is_boolean())return false;
            if(p.contains("growth")&&(!p.at("native_bound").get<bool>()||!validCharacterGrowth(p.at("growth"),p.at("model").get<std::string>())))return false;
            if(p.contains("native_state")) {
                const auto& state=p.at("native_state");const auto& position=state.at("position");
                if(!p.at("native_bound").get<bool>()||!position.is_array()||position.size()!=3||!state.at("alive").is_boolean())return false;
                for(const auto& x:position)if(!x.is_number()||!std::isfinite(x.get<double>())||std::abs(x.get<double>())>1000000)return false;
                const double hp=state.at("hp").get<double>(),max=state.at("max_hp").get<double>();
                if(!std::isfinite(hp)||!std::isfinite(max)||hp<0||max<=0||max>1e12||hp>max*2)return false;
            }
        }
        return !list.empty()&&peers.contains(proto::kHostId);
    } catch(...) {return false;}
}
}
