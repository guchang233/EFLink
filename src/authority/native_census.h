#pragma once
// Read-only build census for an unverified client.
//
// The world adapter resolves every class, method and field by name against the
// running client, so a new game build only needs its metadata names and field
// layout to still match. This census answers exactly that question and nothing
// else: it resolves names and reads IL2CPP metadata. It never calls a game
// method, installs a hook, touches an object or writes game state, so running it
// cannot change gameplay or the official connection.
//
// Its output is the evidence used to extend the allowlist in
// native_build_policy.h. It is not a switch: no adapter starts because of it.
#include "../unity_bridge.h"
#include "native_build_policy.h"
#include "nlohmann/json.hpp"
#include <windows.h>
#include <string>
#include <vector>

namespace eflink::worldauthority {

struct CensusEntry {
    const char* kind;
    std::string name;
    bool ok=false;
    long long offset=-1;
    std::string detail;
};

namespace census_detail {
template<class T> T symbol(const char* name) {
    const auto dll=GetModuleHandleW(L"GameAssembly.dll");
    if(!dll) return nullptr;
    return reinterpret_cast<T>(GetProcAddress(dll,name));
}
inline long long fieldOffset(const void* klass,const char* name,std::string& detail) {
    auto* field=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name");
    auto* offset=symbol<std::size_t(*)(const void*)>("il2cpp_field_get_offset");
    if(!field||!offset) {detail="field metadata unavailable";return -1;}
    const auto* found=field(klass,name);
    if(!found) {detail="field unresolved";return -1;}
    return static_cast<long long>(offset(found));
}
}

// Resolves the signatures the world adapter depends on. `version` is the string
// Application.get_version already reported; it is echoed back as context only.
inline nlohmann::json runNativeCensus(const BE_HostApiV1* api,const std::string& version) {
    std::vector<CensusEntry> entries;
    eflink::unity::Api bridge;bridge.bind(api);

    struct ClassTarget {const char* assembly;const char* space;const char* type;};
    // Order matters only for readability: view tick, event census, scene travel,
    // and the canvas the menu renders into.
    const ClassTarget classes[]={
        {"Gameplay.Beyond.dll","Beyond.Gameplay.View","CameraManager"},
        {"Common.Beyond.dll","Beyond","EventManager"},
        {"Gameplay.Beyond.dll","Beyond.Gameplay.Core","DungeonManager"},
        {"Gameplay.Beyond.dll","Beyond.Gameplay.Actions","GameAction"},
        {"UnityEngine.UIModule.dll","UnityEngine","Canvas"},
    };
    for(const auto& target:classes) {
        CensusEntry entry{"class",std::string(target.space)+"."+target.type,false,-1,{}};
        try {
            const auto resolved=bridge.klass(target.type,target.assembly,target.space);
            entry.ok=resolved.class_info!=nullptr;
            if(!entry.ok) entry.detail="class unresolved";
        } catch(const std::exception& error) {entry.detail=error.what();}
        entries.push_back(std::move(entry));
    }

    struct MethodTarget {const char* assembly;const char* space;const char* type;const char* name;const char* params;const char* returns;};
    const MethodTarget methods[]={
        {"Gameplay.Beyond.dll","Beyond.Gameplay.View","CameraManager","TailLateTick","System.Single","System.Void"},
        {"UnityEngine.UIModule.dll","UnityEngine","Canvas","SendWillRenderCanvases","","System.Void"},
        {"Gameplay.Beyond.dll","Beyond.Gameplay.Core","DungeonManager","LeaveDungeon","","System.Void"},
    };
    for(const auto& target:methods) {
        CensusEntry entry{"method",std::string(target.type)+"."+target.name,false,-1,{}};
        try {
            const auto resolved=bridge.method(target.type,target.name,target.params,target.returns,target.assembly,target.space);
            entry.ok=resolved.method_info!=nullptr;
            if(!entry.ok) entry.detail="method unresolved";
        } catch(const std::exception& error) {entry.detail=error.what();}
        entries.push_back(std::move(entry));
    }

    // The event census walks these three fields by offset, so a shifted layout is
    // the failure mode that matters most on a new build.
    {
        const void* klass=nullptr;
        try {klass=bridge.klass("EventManager","Common.Beyond.dll","Beyond").class_info;}
        catch(const std::exception&) {klass=nullptr;}
        for(const char* name:{"m_gameEvents","m_eventDispatcher","m_evtBindings"}) {
            CensusEntry entry{"field",std::string("EventManager.")+name,false,-1,{}};
            if(!klass) entry.detail="class unresolved";
            else {
                entry.offset=census_detail::fieldOffset(klass,name,entry.detail);
                entry.ok=entry.offset>=0;
            }
            entries.push_back(std::move(entry));
        }
    }

    bool passed=true;
    auto checks=nlohmann::json::array();
    for(const auto& entry:entries) {
        passed=passed&&entry.ok;
        nlohmann::json item={{"kind",entry.kind},{"name",entry.name},{"ok",entry.ok}};
        if(entry.offset>=0) item["offset"]=entry.offset;
        if(!entry.detail.empty()) item["detail"]=entry.detail;
        checks.push_back(std::move(item));
    }
    return {{"game_version",version},{"passed",passed},{"checks",std::move(checks)},
        {"gameplay_modified",false},{"writes",false}};
}

}
