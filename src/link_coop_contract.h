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
inline constexpr unsigned coopContractRevision=20;
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

}
