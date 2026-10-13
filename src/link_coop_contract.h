#pragma once
// Room protocol identity is independent of the transport and public C ABI.
#include "nlohmann/json.hpp"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <stdexcept>

namespace eflink {
inline constexpr unsigned coopWorldSchema=3;
inline constexpr unsigned coopContractRevision=9;
inline constexpr char coopContractName[]="official-room-lifecycle-v1";
inline bool positiveRoomId(const nlohmann::json& value) {
    return value.is_number_integer()&&(value.is_number_unsigned()?value.get<std::uint64_t>()!=0:value.get<std::int64_t>()>0);
}
inline bool validCoopHello(const nlohmann::json& value) {
    try {
        return value.at("world_schema")==coopWorldSchema&&value.at("contract_revision")==coopContractRevision&&
            value.at("contract")==coopContractName&&value.at("build").is_string()&&!value.at("build").get<std::string>().empty()&&value.at("build").get<std::string>().size()<=64&&
            value.at("game_version").is_string()&&!value.at("game_version").get<std::string>().empty()&&value.at("game_version").get<std::string>().size()<=64;
    }catch(...) {return false;}
}

// A level key/server ID may be reused after a native object is released. Only
// the host allocates room lifetimes and control leases; peers echo them back.
class RoomEntityLedger {
    struct Entry {
        std::uint64_t life=0,lease=1,server=0,battle=0,native=0;
        std::uint16_t owner=1;
        bool alive=true;
    };
    std::map<std::string,Entry> entries;
    std::uint64_t nextLife=0;
public:
    void clear() {entries.clear();nextLife=0;}
    void annotate(nlohmann::json& entities) {
        auto candidate=*this;auto annotated=entities;
        candidate.annotateChecked(annotated);
        *this=std::move(candidate);entities=std::move(annotated);
    }
private:
    void annotateChecked(nlohmann::json& entities) {
        if(!entities.is_array()||entities.size()>512)throw std::runtime_error("room entity limit");
        std::set<std::string> present;
        for(auto& entity:entities) {
            const auto key=entity.at("key").get<std::string>();
            if(!present.insert(key).second)throw std::runtime_error("duplicate room entity");
            const auto server=entity.value("server_id",std::uint64_t(0)),battle=entity.value("battle_inst",std::uint64_t(0));
            const auto native=entity.value("native_spawn",std::uint64_t(0));
            const auto owner=entity.value("owner",std::uint16_t(1));
            if(!owner)throw std::runtime_error("room entity owner missing");
            const bool alive=entity.value("alive",entity.at("hp").get<double>()>0);
            auto [it,added]=entries.try_emplace(key);auto& current=it->second;
            if(added||current.server!=server||current.battle!=battle||current.native!=native||(!current.alive&&alive)) {
                if(nextLife==UINT64_MAX)throw std::runtime_error("room lifetime exhausted");
                current={++nextLife,1,server,battle,native,owner,alive};
            } else if(current.owner!=owner) {
                if(current.lease==UINT64_MAX)throw std::runtime_error("room lease exhausted");
                ++current.lease;current.owner=owner;
            }
            current.alive=alive;
            entity["life"]=current.life;entity["lease"]=current.lease;entity["owner"]=current.owner;
            entity.erase("native_spawn"); // a native allocation UID is not a wire identity
        }
        for(auto it=entries.begin();it!=entries.end();) {
            if(!present.contains(it->first))it=entries.erase(it);else ++it;
        }
    }
public:
    nlohmann::json reference(const std::string& key) const {
        auto it=entries.find(key);if(it==entries.end()||!it->second.alive)return nullptr;
        return {{"life",it->second.life},{"lease",it->second.lease},{"owner",it->second.owner}};
    }
    bool matches(const std::string& key,std::uint64_t life,std::uint64_t lease=0,std::uint16_t owner=0) const {
        auto it=entries.find(key);
        return it!=entries.end()&&it->second.alive&&it->second.life==life&&(!lease||it->second.lease==lease)&&
            (!owner||it->second.owner==owner);
    }
    bool assign(const std::string& key,std::uint16_t owner) {
        auto it=entries.find(key);if(it==entries.end()||!owner||!it->second.alive)return false;
        if(it->second.owner!=owner) {
            if(it->second.lease==UINT64_MAX)return false;
            it->second.owner=owner;++it->second.lease;
        }
        return true;
    }
};
}
