#pragma once
#include "world_authority.h"
#include <array>

namespace eflink::worldauthority {
// Public configuration and transform facts of an actual native allocation.
// Gameplay template (for example a doodad config) is not necessarily the
// EntityTemplateData key needed by the common native object allocator.
struct NativeConstructionReading {
    std::string archetype;
    std::array<double,4> rotation{};
    std::array<double,3> scale{};
};
inline void applyNativeConstruction(Observation& observation,const NativeConstructionReading& native) {
    auto state=observation.state;
    state["archetype"]=native.archetype;state["rotation"]=native.rotation;state["scale"]=native.scale;
    if(!validStateForKind(observation.kind,state))throw std::runtime_error("native construction metadata is incomplete or invalid");
    observation.state.swap(state);
}
}
