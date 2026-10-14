#pragma once
#include "world_authority.h"
#include <array>

namespace eflink::worldauthority {
// Values read from native Entity/AbilitySystem getters. No guest numerical
// claim, prediction, reward or appearance result enters this translation.
struct NativeBattleReading {
    std::string kind,key,model;std::uint64_t cookie=0;
    std::array<double,3> position{};
    double hp=0,maxHp=0,poise=0,maxPoise=0;bool alive=false;
};
inline Observation nativeBattleObservation(const NativeBattleReading& native) {
    Json state={{"template",native.model},{"position",native.position},{"hp",native.hp},{"max_hp",native.maxHp},{"dead",!native.alive}};
    // Some native actors have no poise capacity. Absence does not invent a bar.
    if(native.poise!=0||native.maxPoise!=0) {state["poise"]=native.poise;state["max_poise"]=native.maxPoise;}
    if((native.kind!="player"&&native.kind!="enemy")||native.key.empty()||native.key.size()>160||
       native.model.empty()||native.model.size()>256||!native.cookie||!validState(state))
        throw std::runtime_error("invalid native battle observation");
    return {native.key,native.kind,std::move(state),native.cookie};
}
}
