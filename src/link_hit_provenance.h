#pragma once
#include "link_native_identity.h"
#include "link_coop_contract.h"
#include <optional>
#include <tuple>

namespace eflink {
// Native skill addresses/cast IDs are private lookup keys only. Claims use a
// room cast ID; they never carry a client's computed damage or account IDs.
class NativeCastBindings {
    using Key=std::pair<std::uintptr_t,std::uint32_t>;
    struct Entry {std::uint32_t room;std::uint64_t actor,epoch,cast,seen;NativeIdentity owner;};
    std::map<Key,Entry> entries;
public:
    void clear() {entries.clear();}
    void retain(std::uint32_t room,std::uint64_t actor,std::uint64_t epoch,std::uint64_t now) {
        for(auto it=entries.begin();it!=entries.end();) {
            const auto& e=it->second;
            if(e.room!=room||e.actor!=actor||e.epoch!=epoch||now<e.seen||now-e.seen>=30000)it=entries.erase(it);else ++it;
        }
    }
    bool bind(std::uintptr_t skill,std::uint32_t nativeCast,std::uint32_t room,std::uint64_t actor,
        std::uint64_t epoch,std::uint64_t cast,const NativeIdentity& owner,std::uint64_t now) {
        if(!skill||!nativeCast||!room||!actor||!epoch||!cast||!owner.matches(owner))return false;
        retain(room,actor,epoch,now);
        auto it=entries.find({skill,nativeCast});
        if(it!=entries.end())return it->second.cast==cast&&it->second.owner.matches(owner);
        if(entries.size()>=512)return false;
        entries.emplace(Key{skill,nativeCast},Entry{room,actor,epoch,cast,now,owner});return true;
    }
    std::optional<std::uint64_t> resolve(std::uintptr_t skill,std::uint32_t nativeCast,std::uint32_t room,
        std::uint64_t actor,std::uint64_t epoch,const NativeIdentity& owner,std::uint64_t now) const {
        auto it=entries.find({skill,nativeCast});if(it==entries.end())return {};
        const auto& e=it->second;
        if(e.room!=room||e.actor!=actor||e.epoch!=epoch||!e.owner.matches(owner)||now<e.seen||now-e.seen>=30000)return {};
        return e.cast;
    }
    std::size_t size() const {return entries.size();}
};
inline bool validHitClaim(const nlohmann::json& claim) {
    try {
        if(!claim.is_object()||claim.size()!=8||!positiveRoomId(claim.at("actor"))||!positiveRoomId(claim.at("epoch"))||
            !positiveRoomId(claim.at("cast"))||!positiveRoomId(claim.at("life"))||!positiveRoomId(claim.at("strike"))||
            !claim.at("key").is_string()||claim.at("key").get<std::string>().empty()||claim.at("key").get<std::string>().size()>128)return false;
        for(const auto* field:{"action","unit"})if(!claim.at(field).is_number_integer()||claim.at(field).get<std::int64_t>()<0||claim.at(field).get<std::uint64_t>()>1000000)return false;
        return true;
    }catch(...) {return false;}
}
class HitClaimLedger {
    using Key=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,std::string,std::uint64_t,std::uint64_t>;
    std::map<Key,std::uint64_t> claims;
public:
    void clear() {claims.clear();}
    bool accept(const nlohmann::json& claim,std::uint64_t now) {
        if(!validHitClaim(claim))return false;
        for(auto it=claims.begin();it!=claims.end();) {
            if(now>=it->second&&now-it->second>=30000)it=claims.erase(it);else ++it;
        }
        Key key{claim.at("actor"),claim.at("epoch"),claim.at("cast"),claim.at("key"),claim.at("life"),claim.at("strike")};
        // An occurrence cannot change action/unit to acquire another identity.
        if(claims.contains(key)||claims.size()>=4096)return false;
        claims.emplace(std::move(key),now);return true;
    }
};
}
