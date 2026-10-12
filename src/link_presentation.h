#pragma once
// Presentation data never authorizes a hit, reward, skill cast or account mutation.
#include "nlohmann/json.hpp"
#include <cmath>
#include <set>
#include <cstdint>
namespace eflink {
inline bool presentationVector(const nlohmann::json& a,unsigned n,double bound=1e6) {
    if(!a.is_array()||a.size()!=n)return false;
    for(const auto& v:a)if(!v.is_number()||!std::isfinite(v.get<double>())||std::abs(v.get<double>())>bound)return false;
    return true;
}
inline bool presentationRotation(const nlohmann::json& a) {
    if(!presentationVector(a,4,1.1))return false;
    double length=0;for(const auto& v:a)length+=v.get<double>()*v.get<double>();return length>=.5&&length<=1.5;
}
inline bool presentationResource(const std::string& s) {
    return !s.empty()&&s.size()<=256&&s.find("..") == std::string::npos&&
        s.find_first_of(":\\\r\n") == std::string::npos;
}
inline bool validPresentation(const nlohmann::json& p) {
    try {
        if(!p.at("sample_time").is_number_unsigned()||!p.at("sample_time").get<std::uint64_t>()||p.at("version")!=1||!p.at("seq").is_number_unsigned()||!p.at("seq").get<std::uint64_t>()||
           !p.at("members").is_array()||p.at("members").size()>4||!p.at("effects").is_array()||p.at("effects").size()>96)return false;
        std::set<unsigned> slots;std::set<std::uint64_t> instances;unsigned controlled=0;
        for(const auto& m:p.at("members")) {
            auto slot=m.at("slot").get<unsigned>();auto model=m.at("model").get<std::string>();
            if(!m.at("slot").is_number_integer()||!m.at("flags").is_number_integer()||m.at("flags").get<int>()<0||m.at("flags").get<int>()>255||slot>=4||!slots.insert(slot).second||!m.at("life").is_number_unsigned()||!m.at("life").get<std::uint64_t>()||
               !presentationResource(model)||model.find("chr_")==std::string::npos||
               !presentationVector(m.at("position"),3)||!presentationRotation(m.at("rotation"))||
               !presentationVector(m.at("root"),3,100)||!m.at("bones").is_array()||m.at("bones").size()>384||
               !m.at("names").is_array()||m.at("bones").size()!=m.at("names").size()*4)return false;
            if(m.at("controlled").get<bool>())++controlled;
            for(const auto& b:m.at("bones"))if(!b.is_number_integer()||std::abs(b.get<int>())>32767)return false;
            for(const auto& name:m.at("names"))if(!name.is_string()||name.get<std::string>().size()>80)return false;
        }
        if(controlled>1)return false;
        for(const auto& e:p.at("effects")) {
            if(!e.at("id").is_number_unsigned()||!e.at("id").get<std::uint64_t>()||!instances.insert(e.at("id").get<std::uint64_t>()).second||
               !presentationResource(e.at("name").get<std::string>())||!presentationVector(e.at("position"),3)||
               !presentationRotation(e.at("rotation"))||!presentationVector(e.at("scale"),3,100))return false;
        }
        if(p.contains("material_vfx")) {
            if(!p.at("material_vfx").is_array()||p.at("material_vfx").size()>64)return false;
            std::set<std::uint64_t> materialIds;
            for(const auto& v:p.at("material_vfx")) {
                if(!v.at("id").is_number_unsigned()||!v.at("id").get<std::uint64_t>()||!materialIds.insert(v.at("id").get<std::uint64_t>()).second||
                   !presentationResource(v.at("name").get<std::string>())||!v.at("path").is_string()||v.at("path").get<std::string>().size()>512||
                   v.at("path").get<std::string>().find("..")!=std::string::npos)return false;
                bool found=false;for(const auto& m:p.at("members"))if(v.at("slot")==m.at("slot")&&v.at("life")==m.at("life"))found=true;
                if(!found)return false;
            }
        }
        return true;
    }catch(...) {return false;}
}
inline bool validPoise(const nlohmann::json& p) {
    try {const double value=p.at("value").get<double>(),max=p.at("max").get<double>();
        return std::isfinite(value)&&std::isfinite(max)&&max>=0&&max<=1e9&&value>=0&&value<=max+1;
    }catch(...) {return false;}
}
}
