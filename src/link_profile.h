#pragma once
#include "nlohmann/json.hpp"
#include <string>

namespace eflink {
inline std::string characterTemplate(const std::string& path) {
    auto begin=path.find_last_of('/');begin=begin==std::string::npos?0:begin+1;
    const std::string suffix="_postmodel.prefab";
    if(!path.ends_with(suffix))return {};
    auto name=path.substr(begin,path.size()-begin-suffix.size());
    if(name.rfind("chr_",0)!=0||name.size()>80||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)return {};
    return name;
}
// Public growth inputs only. Native config limits are checked by the game adapter
// before allocation; client HP/attack values and account instance IDs never travel.
inline bool validCharacterGrowth(const nlohmann::json& value,const std::string& model) {
    try {
        if(!value.is_object()||value.size()!=4||characterTemplate(model).empty()||
           value.at("template").get<std::string>()!=characterTemplate(model))return false;
        for(const char* key:{"level","break_stage","potential"})if(!value.at(key).is_number_integer())return false;
        const auto level=value.at("level").get<std::int64_t>(),stage=value.at("break_stage").get<std::int64_t>(),potential=value.at("potential").get<std::int64_t>();
        return level>=1&&level<=1000&&stage>=0&&stage<=64&&potential>=0&&potential<=64;
    }catch(...) {return false;}
}
// Presentation only: public weapon template, never an account instance ID or
// claimed combat stats. Zero means an observed unequipped character; null means
// no complete official data. This does not grant native readiness or admission.
inline bool validCharacterAppearance(const nlohmann::json& value,const std::string& model) {
    try {
        if(!value.is_object()||value.size()!=2||characterTemplate(model).empty()||
           !value.at("template").is_string()||value.at("template").get<std::string>()!=characterTemplate(model)||
           !value.at("weapon_template").is_number_integer())return false;
        const auto weapon=value.at("weapon_template").get<std::int64_t>();
        return weapon>=0&&weapon<=2147483647;
    }catch(...) {return false;}
}
}
