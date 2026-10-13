#pragma once
#include "world_authority.h"
#include <array>

namespace eflink::worldauthority {
struct MineralReading {
    std::string key,model;std::array<double,3> position{};
    std::uint64_t cookie=0;int propertyHp=0,runtimeHp=0,maximum=0;bool collected=false;
};
inline Observation mineralObservation(const MineralReading& native) {
    // The native property callback can reconcile runtime prediction upward.
    // Runtime HP and a dispatched attack are therefore NOT committed durability.
    Json state={{"template",native.model},{"position",native.position},{"hp_ratio",native.propertyHp},
        {"max_hp_ratio",native.maximum},{"collected",native.collected}};
    if(native.key.empty()||native.key.size()>160||native.model.empty()||native.model.size()>256||!native.cookie||
       native.maximum<=0||!validState(state))throw std::runtime_error("invalid native mineral property observation");
    return {native.key,"mineral",std::move(state),native.cookie};
}
inline Json mineralReadback(const MineralReading& native) {
    auto observation=mineralObservation(native);auto diagnostic=observation.state;
    diagnostic["key"]=native.key;diagnostic["kind"]="mineral";
    diagnostic["runtime_hp_ratio"]=native.runtimeHp;diagnostic["runtime_diverged"]=native.runtimeHp!=native.propertyHp;
    return diagnostic; // diagnostics only; never submitted as shared object state
}
}
