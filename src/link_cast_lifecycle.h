#pragma once
#include "link_coop_contract.h"
#include "link_profile.h"
#include <map>
#include <set>

namespace eflink {
// No native pointers or account IDs. A cast belongs to a room actor and scene,
// rather than to a reusable skill name. Terminal events leave tombstones.
class RoomCastLedger {
    using Json=nlohmann::json;
    struct Entry {Json event;std::uint64_t seen=0,started=0;bool began=false;};
    std::map<std::pair<std::uint64_t,std::uint64_t>,Entry> entries;
    std::map<std::uint64_t,std::uint64_t> latestCast;
public:
    static constexpr std::size_t maxActive=256;
    void clear() {entries.clear();latestCast.clear();}
    void retain(const std::set<std::uint64_t>& actors) {
        for(auto it=entries.begin();it!=entries.end();) {
            if(!actors.contains(it->first.first))it=entries.erase(it);else ++it;
        }
        for(auto it=latestCast.begin();it!=latestCast.end();) {
            if(!actors.contains(it->first))it=latestCast.erase(it);else ++it;
        }
    }
    bool accept(const Json& event,std::uint16_t peer,std::uint64_t actor,std::uint64_t epoch,const std::string& model,std::uint64_t now) {
        try {
            if(!event.is_object()||event.size()!=10||!peer||!actor||!epoch||event.at("peer")!=peer||event.at("actor")!=actor||
                event.at("epoch")!=epoch||event.at("model")!=model||characterTemplate(model).empty()||
                !positiveRoomId(event.at("peer"))||!positiveRoomId(event.at("actor"))||!positiveRoomId(event.at("epoch"))||
                !positiveRoomId(event.at("cast"))||!positiveRoomId(event.at("seq")))return false;
            const auto cast=event.at("cast").get<std::uint64_t>(),seq=event.at("seq").get<std::uint64_t>();
            const auto phase=event.at("phase").get<std::string>(),skill=event.at("skill").get<std::string>(),key=event.at("key").get<std::string>();
            if((phase!="begin"&&phase!="end"&&phase!="interrupt")||skill.empty()||skill.size()>128||key.size()>128||
                !event.at("life").is_number_integer()||(key.empty()?event.at("life")!=0:!positiveRoomId(event.at("life"))))return false;
            const auto id=std::make_pair(actor,cast);auto found=entries.find(id);
            if(found!=entries.end()) {
                auto& previous=found->second;
                if(phase=="begin"||previous.event.at("phase")!="begin"||seq<=previous.event.at("seq").get<std::uint64_t>()||
                    event.at("skill")!=previous.event.at("skill")||event.at("key")!=previous.event.at("key")||event.at("life")!=previous.event.at("life"))return false;
                previous.event=event;previous.seen=now;return true;
            }
            if(latestCast.contains(actor)&&cast<=latestCast.at(actor))return false;
            if(latestCast.size()>=32&&!latestCast.contains(actor))return false;
            for(auto it=entries.begin();it!=entries.end();) {
                if(now>=it->second.seen&&now-it->second.seen>=30000)it=entries.erase(it);else ++it;
            }
            if(entries.size()>=1024)return false;
            if(phase=="begin") {
                std::size_t activeCount=0;
                for(const auto& [identity,entry]:entries)if(entry.event.at("phase")=="begin")++activeCount;
                if(activeCount>=maxActive)return false;
            }
            latestCast[actor]=cast;entries.emplace(id,Entry{event,now,now,phase=="begin"});return true;
        }catch(...) {return false;}
    }
    Json active(std::uint64_t now) const {
        auto result=Json::array();for(const auto& [id,entry]:entries)
            if(entry.event.at("phase")=="begin"&&now>=entry.seen&&now-entry.seen<30000)result.push_back(entry.event);
        return result;
    }
    bool permitsClaim(const Json& claim,std::uint16_t peer,std::uint64_t now) const {
        try {
            auto it=entries.find({claim.at("actor").get<std::uint64_t>(),claim.at("cast").get<std::uint64_t>()});
            if(it==entries.end()||!it->second.began||now<it->second.started||now-it->second.started>=30000)return false;
            const auto& e=it->second.event;
            return e.at("peer")==peer&&e.at("epoch")==claim.at("epoch")&&e.at("key")==claim.at("key")&&
                e.at("life")==claim.at("life")&&e.at("phase")!="interrupt";
        }catch(...) {return false;}
    }
};
}
