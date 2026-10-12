#pragma once
// Wire-only projection: use stable IDs instead of array indices, so a removed
// enemy cannot shift a patch onto a different lifetime. Application is atomic.
#include "nlohmann/json.hpp"
#include <string>
#include <stdexcept>

namespace eflink {
inline nlohmann::json keyedWorld(const nlohmann::json& world) {
    auto result=world;
    for(const auto& group:{std::string("entities"),std::string("players")}) {
        const char* id=group=="entities"?"key":"peer";
        const auto& input=world.at(group);
        if(!input.is_array()||input.size()>(group=="entities"?512u:32u))throw std::runtime_error("delta group limit");
        auto mapped=nlohmann::json::object();
        for(const auto& entry:input) {
            const auto key=group=="entities"?entry.at(id).get<std::string>():std::to_string(entry.at(id).get<std::uint16_t>());
            if(key.empty()||key.size()>128||mapped.contains(key))throw std::runtime_error("delta group identity");
            mapped[key]=entry;
        }
        result[group]=std::move(mapped);
    }
    return result;
}
inline nlohmann::json arrayWorld(nlohmann::json keyed) {
    for(const auto& group:{std::string("entities"),std::string("players")}) {
        auto& input=keyed.at(group);
        if(!input.is_object()||input.size()>(group=="entities"?512u:32u))throw std::runtime_error("delta group limit");
        auto output=nlohmann::json::array();
        for(auto it=input.begin();it!=input.end();++it) {
            const auto id=group=="entities"?it.value().at("key").get<std::string>():std::to_string(it.value().at("peer").get<std::uint16_t>());
            if(id!=it.key())throw std::runtime_error("delta group identity changed");
            output.push_back(it.value());
        }
        input=std::move(output);
    }
    return keyed;
}
inline bool validWorldDelta(const nlohmann::json& delta) {
    try {
        if(!delta.is_array()||delta.size()>8192)return false;
        for(const auto& operation:delta) {
            const auto op=operation.at("op").get<std::string>(),path=operation.at("path").get<std::string>();
            if((op!="add"&&op!="remove"&&op!="replace")||path.empty()||path[0]!='/'||path.size()>512||
                operation.size()!=(op=="remove"?2u:3u)||(op!="remove"&&!operation.contains("value")))return false;
        }
        return true;
    }catch(...) {return false;}
}
inline nlohmann::json worldDelta(const nlohmann::json& before,const nlohmann::json& after) {
    auto delta=nlohmann::json::diff(keyedWorld(before),keyedWorld(after));
    if(!validWorldDelta(delta))throw std::runtime_error("delta operation limit");return delta;
}
inline nlohmann::json applyWorldDelta(const nlohmann::json& before,const nlohmann::json& delta) {
    if(!validWorldDelta(delta))throw std::runtime_error("invalid world delta");
    return arrayWorld(keyedWorld(before).patch(delta));
}
}
