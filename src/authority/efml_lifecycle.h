#pragma once
#include "BetterEndfield/ThirdPartyModule.h"
#include <cstddef>
#include <cstdint>

namespace eflink::worldauthority {
// EFML's optional shutdown barrier is a tail on its host/module ABI. The
// Better-Endfield V1 prefix stays byte-for-byte unchanged for older hosts.
struct EfmlHostExtensionV1 {
    std::uint32_t struct_size,version;
    const char *host_version,*data_root;
    BE_Result(BE_CALL* post_to_game_thread)(void*,void(BE_CALL*)(void*),void*);
    void* remaining_callbacks[8];
    std::uint32_t feature_flags;
};
struct EfmlHostV1 {BE_ThirdPartyHostV1 base;const EfmlHostExtensionV1* efml;};
struct EfmlModuleWithShutdownBarrierV1 {
    BE_ThirdPartyModuleV1 base;
    BE_Result(BE_CALL* prepare_shutdown)(void);
};
static_assert(offsetof(EfmlHostExtensionV1,feature_flags)==96);
static_assert(sizeof(EfmlHostExtensionV1)==104);
static_assert(offsetof(EfmlModuleWithShutdownBarrierV1,prepare_shutdown)==sizeof(BE_ThirdPartyModuleV1));
constexpr std::uint32_t shutdownBarrierFeature=1;
inline const EfmlHostExtensionV1* shutdownBarrierExtension(const BE_ThirdPartyHostV1* host) {
    if(!host||host->struct_size<sizeof(EfmlHostV1))return nullptr;
    const auto* ext=reinterpret_cast<const EfmlHostV1*>(host)->efml;
    return ext&&ext->struct_size>=sizeof(EfmlHostExtensionV1)&&ext->version==1&&
        (ext->feature_flags&shutdownBarrierFeature)!=0&&ext->post_to_game_thread?ext:nullptr;
}
inline bool hasShutdownBarrier(const BE_ThirdPartyHostV1* host) {return shutdownBarrierExtension(host)!=nullptr;}
}
