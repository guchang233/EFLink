#pragma once
#include "nlohmann/json.hpp"
#include <string>

namespace eflink {
// Capability facts are supplied by a game adapter, never by a menu/configuration.
// A loaded scene or matched replica is not proof of an authoritative world.
struct AuthorityFacts {
    bool isolatedWorld=false,logicalPlayers=false,combat=false,sceneRules=false,nativeDamageVerified=false;
    bool complete() const {return isolatedWorld&&logicalPlayers&&combat&&sceneRules&&nativeDamageVerified;}
    nlohmann::json json() const {return {{"isolated_world",isolatedWorld},{"logical_players",logicalPlayers},
        {"combat",combat},{"scene_rules",sceneRules},{"native_damage_verified",nativeDamageVerified}};}
    nlohmann::json blockers() const {
        auto out=nlohmann::json::array();
        if(!isolatedWorld) out.push_back("host_world_isolation");
        if(!logicalPlayers) out.push_back("host_logical_players");
        if(!combat) out.push_back("host_combat_settlement");
        if(!sceneRules) out.push_back("host_scene_rules");
        if(!nativeDamageVerified)out.push_back("host_native_damage_verification");
        return out;
    }
};
struct ReplicaFacts {
    bool mapLoaded=false,entitiesReady=false,worldIsolated=false,logicalPlayers=false;
    bool complete() const {return mapLoaded&&entitiesReady&&worldIsolated&&logicalPlayers;}
    nlohmann::json json() const {return {{"map_loaded",mapLoaded},{"entities_ready",entitiesReady},
        {"world_isolated",worldIsolated},{"logical_players",logicalPlayers}};}
    static ReplicaFacts parse(const nlohmann::json& j) {
        return {j.at("map_loaded").get<bool>(),j.at("entities_ready").get<bool>(),
            j.at("world_isolated").get<bool>(),j.at("logical_players").get<bool>()};
    }
    nlohmann::json blockers() const {
        auto out=nlohmann::json::array();
        if(!mapLoaded) out.push_back("guest_map_loading");
        if(!entitiesReady) out.push_back("guest_entity_replicas");
        if(!worldIsolated) out.push_back("guest_world_isolation");
        if(!logicalPlayers) out.push_back("guest_logical_players");
        return out;
    }
};
}
