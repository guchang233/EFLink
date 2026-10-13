#pragma once
// Resource identities and requests are independent of native game pointers.
// Observing a resource does not certify that its operation/recovery adapter works.
#include "link_coop_contract.h"
#include <cmath>
#include <deque>
#include <limits>
#include <optional>
#include <vector>
#include <tuple>

namespace eflink {
inline bool validResourceObservation(const nlohmann::json& value,bool wire=false) {
    try {
        static const std::set<std::string> fields={"key","kind","template","position","hp_ratio","max_hp_ratio","collected","life","version"};
        if(!value.is_object())return false;
        for(auto it=value.begin();it!=value.end();++it)if(!fields.contains(it.key()))return false;
        const auto key=value.at("key").get<std::string>(),model=value.at("template").get<std::string>();
        if(key.empty()||key.size()>160||model.empty()||model.size()>256||value.at("kind")!="mine"||!value.at("collected").is_boolean())return false;
        const auto& pos=value.at("position");if(!pos.is_array()||pos.size()!=3)return false;
        for(const auto& x:pos)if(!x.is_number()||!std::isfinite(x.get<double>())||std::abs(x.get<double>())>1000000)return false;
        if(!value.at("hp_ratio").is_number_integer()||!value.at("max_hp_ratio").is_number_integer())return false;
        const auto hp=value.at("hp_ratio").get<std::int64_t>(),max=value.at("max_hp_ratio").get<std::int64_t>();
        if(hp<0||max<=0||max>100000000||hp>max)return false;
        return wire?value.size()==9&&positiveRoomId(value.at("life"))&&positiveRoomId(value.at("version")):value.size()==7;
    }catch(...) {return false;}
}
inline bool validResourceBaseline(const nlohmann::json& objects) {
    if(!objects.is_array()||objects.size()>512)return false;
    std::set<std::string> keys;
    for(const auto& object:objects)if(!validResourceObservation(object,true)||!keys.insert(object.at("key").get<std::string>()).second)return false;
    return true;
}
class ResourceAuthority {
public:
    using Json=nlohmann::json;
    struct Observation {Json state;std::uint64_t incarnation=0;};
private:
    struct Object {Json state;std::uint64_t incarnation=0,confirmedVersion=0;};
    struct Request {Json body;std::uint16_t peer=0;std::uint64_t expires=0,beforeVersion=0;};
    std::uint32_t room=0;std::uint64_t epoch=0,nextLife=0;
    std::map<std::string,Object> objects;
    std::map<std::pair<std::uint16_t,std::uint64_t>,std::uint64_t> sequences;
    using StrikeKey=std::tuple<std::uint16_t,std::uint64_t,std::uint64_t,std::string,std::uint64_t>;
    std::map<StrikeKey,std::uint64_t> strikes;
    std::map<std::tuple<std::uint16_t,std::uint64_t,std::uint64_t>,Request> pending;
public:
    void reset(std::uint32_t r=0,std::uint64_t e=0) {
        room=r;epoch=e;objects.clear();sequences.clear();strikes.clear();pending.clear();
        // Keep the allocator monotonic, including an epoch reused by a new room.
    }
    Json observe(const std::vector<Observation>& values) {
        if(!room||!epoch||values.size()>512)throw std::runtime_error("resource scene unavailable");
        auto candidate=objects;auto life=nextLife;std::set<std::string> present;
        for(const auto& value:values) {
            if(!value.incarnation||!validResourceObservation(value.state))throw std::runtime_error("invalid native resource observation");
            const auto key=value.state.at("key").get<std::string>();
            if(!present.insert(key).second)throw std::runtime_error("ambiguous native resource identity");
            auto it=candidate.find(key);Json state=value.state;
            bool replaced=it==candidate.end()||it->second.incarnation!=value.incarnation;
            if(!replaced) {
                auto prior=it->second.state;prior.erase("life");prior.erase("version");
                // The native object may be pooled in place or regenerate without reallocating.
                replaced=prior.at("template")!=state.at("template")||prior.at("position")!=state.at("position")||
                    (prior.at("collected").get<bool>()&&!state.at("collected").get<bool>());
                state["life"]=it->second.state.at("life");
                auto version=it->second.state.at("version").get<std::uint64_t>();
                if(prior!=value.state) {if(version==UINT64_MAX)throw std::runtime_error("resource revision exhausted");++version;}
                state["version"]=version;
            }
            if(replaced) {if(life==UINT64_MAX)throw std::runtime_error("resource lifetime exhausted");state["life"]=++life;state["version"]=1ull;}
            const auto confirmed=!replaced?it->second.confirmedVersion:0;
            candidate[key]={std::move(state),value.incarnation,confirmed};
        }
        for(auto it=candidate.begin();it!=candidate.end();)if(!present.contains(it->first))it=candidate.erase(it);else ++it;
        objects=std::move(candidate);nextLife=life;
        for(auto it=strikes.begin();it!=strikes.end();) {
            auto object=objects.find(std::get<3>(it->first));
            if(object==objects.end()||object->second.state.at("life")!=std::get<4>(it->first))it=strikes.erase(it);else ++it;
        }
        for(auto it=pending.begin();it!=pending.end();) {
            const auto object=objects.find(it->second.body.at("key").get<std::string>());
            if(object==objects.end()||object->second.state.at("life")!=it->second.body.at("life"))it=pending.erase(it);else ++it;
        }
        return baseline();
    }
    Json baseline() const {Json result=Json::array();for(const auto& [key,obj]:objects)result.push_back(obj.state);return result;}
    Json reference(const std::string& key) const {auto it=objects.find(key);return it==objects.end()?Json{}:it->second.state;}
    bool matches(const Json& request) const {
        try {auto it=objects.find(request.at("key").get<std::string>());
            return it!=objects.end()&&request.at("life")==it->second.state.at("life")&&!it->second.state.at("collected").get<bool>();
        }catch(...) {return false;}
    }
    bool submit(const Json& request,std::uint16_t sender,std::uint64_t actor,bool adapterReady,std::uint64_t now) {
        try {
            static const std::set<std::string> allowed={"room","epoch","peer","actor","request","key","life","cast","strike","operation"};
            if(!adapterReady||!sender||!actor||request.size()!=allowed.size()||pending.size()>=128)return false;
            for(auto it=request.begin();it!=request.end();++it)if(!allowed.contains(it.key()))return false;
            if(request.at("room")!=room||request.at("epoch")!=epoch||request.at("peer")!=sender||request.at("actor")!=actor||
               request.at("operation")!="mine"||!matches(request))return false;
            for(const char* field:{"request","life","cast","strike"})if(!positiveRoomId(request.at(field)))return false;
            const auto seq=request.at("request").get<std::uint64_t>();
            const auto source=std::make_pair(sender,actor);
            const StrikeKey strikeKey{sender,actor,request.at("cast").get<std::uint64_t>(),request.at("key").get<std::string>(),request.at("life").get<std::uint64_t>()};
            const auto strike=request.at("strike").get<std::uint64_t>();
            // Reliable requests are ordered per sender. A stale sequence never becomes executable again.
            if(seq<=sequences[source]||(strikes.contains(strikeKey)&&strike<=strikes.at(strikeKey))||
               (strikes.size()>=4096&&!strikes.contains(strikeKey))||pending.contains({sender,actor,seq}))return false;
            sequences[source]=seq;strikes[strikeKey]=strike;
            pending[{sender,actor,seq}]={request,sender,now+1500,objects.at(request.at("key").get<std::string>()).state.at("version").get<std::uint64_t>()};return true;
        }catch(...) {return false;}
    }
    // A native void call is not a confirmation. Only a matching observation
    // proving the operation happened may finalize a pending request.
    std::optional<Json> confirm(std::uint16_t peer,std::uint64_t actor,std::uint64_t request,std::uint64_t observedLife,
        std::uint64_t observedVersion,bool nativeAccepted,std::uint64_t now) {
        auto it=pending.find({peer,actor,request});if(it==pending.end())return {};
        const auto& p=it->second;
        auto object=objects.find(p.body.at("key").get<std::string>());
        if(!nativeAccepted||now>p.expires||object==objects.end()||p.body.at("life")!=observedLife||
           object->second.state.at("life")!=observedLife||object->second.state.at("version")!=observedVersion||
           observedVersion<=p.beforeVersion||observedVersion<=object->second.confirmedVersion)return {};
        Json result={{"room",room},{"epoch",epoch},{"peer",peer},{"actor",actor},{"request",request},{"cast",p.body.at("cast")},{"strike",p.body.at("strike")},{"key",p.body.at("key")},
            {"life",observedLife},{"version",observedVersion},{"state",object->second.state}};
        object->second.confirmedVersion=observedVersion;pending.erase(it);return result;
    }
    void revokePeer(std::uint16_t peer) {for(auto it=pending.begin();it!=pending.end();)if(std::get<0>(it->first)==peer)it=pending.erase(it);else ++it;}
    void expire(std::uint64_t now) {for(auto it=pending.begin();it!=pending.end();)if(now>it->second.expires)it=pending.erase(it);else ++it;}
    std::size_t pendingCount() const {return pending.size();}
};
}
