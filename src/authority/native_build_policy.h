#pragma once
#include "../unity_bridge.h"

namespace eflink::worldauthority {
// This allowlist proves the read-only metadata/census signatures only. It does
// not certify any shared gameplay, official packet apply or account restoration.
// 1.6.3: offline metadata check on 2026-11-05 found every method the adapters call
// (95) and every field they read except FacMarkInstRuntimeData.isTpRelated, which
// was renamed to isTravelRelated and is handled. Runtime resolution still fails
// closed per method; certificates (execution/isolation/recovery) stay false.
inline bool readOnlyBuildSupported(const std::string& version) {return version=="1.5.3"||version=="1.6.3";}
inline std::string nativeGameVersion(const BE_HostApiV1* api) {
    if(!api)throw std::runtime_error("native runtime unavailable");
    eflink::unity::Api bridge;bridge.bind(api);
    auto* object=bridge.call(bridge.method("Application","get_version","","System.String"));
    const auto dll=GetModuleHandleW(L"GameAssembly.dll");
    auto length=reinterpret_cast<int(*)(void*)>(GetProcAddress(dll,"il2cpp_string_length"));
    auto chars=reinterpret_cast<const wchar_t*(*)(void*)>(GetProcAddress(dll,"il2cpp_string_chars"));
    if(!object||!length||!chars)throw std::runtime_error("native game version unavailable");
    const auto count=length(object);if(count<1||count>32)throw std::runtime_error("invalid native game version");
    std::string version;const auto* text=chars(object);
    for(int i=0;i<count;++i) {if(text[i]<32||text[i]>126)throw std::runtime_error("invalid native game version");version.push_back(char(text[i]));}
    return version;
}
}
