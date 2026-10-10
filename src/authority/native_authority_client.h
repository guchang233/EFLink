#pragma once
// C++ ownership/serialization boundary for the Zig kernel. No game calls.
#include "EndfieldLink/NativeAuthority.h"
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace eflink::authority {
class NativeAuthorityClient {
    HMODULE library=nullptr;
    const EFLinkNative_ApiV1* api=nullptr;
    EFLinkNative_Context* context=nullptr;
    std::mutex mutex;
    static void require(std::int32_t result) {
        if(result!=EFLinkNative_Ok)throw std::runtime_error("native authority rejected call: "+std::to_string(result));
    }
    static std::uint32_t size(std::span<const unsigned char> bytes) {
        if(bytes.empty()||bytes.size()>1024*1024)throw std::runtime_error("native body exceeds the kernel limit");
        return static_cast<std::uint32_t>(bytes.size());
    }
public:
    struct Message {EFLinkNative_Message head{};std::vector<unsigned char> body;};
    NativeAuthorityClient(const std::filesystem::path& path,std::uint64_t world,std::uint64_t scene,std::int32_t sceneNum) {
        if(!path.is_absolute())throw std::runtime_error("native authority DLL needs an absolute path");
        library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!library)throw std::runtime_error("native authority DLL could not be loaded");
        try {
            const auto entry=reinterpret_cast<EFLinkNative_GetApiV1Fn>(GetProcAddress(library,EFLINK_NATIVE_ENTRY));
            api=entry?entry():nullptr;
            if(!api||api->version!=1||api->struct_size<sizeof(EFLinkNative_ApiV1)||!api->create||!api->destroy||!api->add_member||
                !api->import_host_scene||!api->allocate_approved_operator||!api->begin_scene||!api->scene_loaded||
                !api->native_applied||!api->poll||!api->remove_member)throw std::runtime_error("native authority C ABI mismatch");
            require(api->create(world,scene,sceneNum,&context));
            if(!context)throw std::runtime_error("native authority returned an empty context");
        }catch(...) {if(context&&api&&api->destroy)api->destroy(context);FreeLibrary(library);throw;}
    }
    ~NativeAuthorityClient() {if(context)api->destroy(context);if(library)FreeLibrary(library);}
    NativeAuthorityClient(const NativeAuthorityClient&)=delete;
    NativeAuthorityClient& operator=(const NativeAuthorityClient&)=delete;
    void addMember(std::uint16_t member) {std::lock_guard lock(mutex);require(api->add_member(context,member));}
    void removeMember(std::uint16_t member) {std::lock_guard lock(mutex);require(api->remove_member(context,member));}
    void importHostScene(std::uint16_t member,std::span<const unsigned char> bytes) {
        const auto count=size(bytes);std::lock_guard lock(mutex);require(api->import_host_scene(context,member,bytes.data(),count));
    }
    std::uint64_t allocateApprovedOperator(std::uint16_t member,std::span<const unsigned char> bytes) {
        const auto count=size(bytes);std::lock_guard lock(mutex);std::uint64_t id=0;
        require(api->allocate_approved_operator(context,member,bytes.data(),count,&id));return id;
    }
    std::uint64_t beginScene(std::uint16_t member,std::uint64_t role,std::uint64_t privateGeneration) {
        std::lock_guard lock(mutex);std::uint64_t transition=0;
        require(api->begin_scene(context,member,role,privateGeneration,&transition));return transition;
    }
    void sceneLoaded(std::uint16_t member,std::uint64_t world,std::uint64_t transition,std::span<const unsigned char> bytes) {
        const auto count=size(bytes);std::lock_guard lock(mutex);require(api->scene_loaded(context,member,world,transition,bytes.data(),count));
    }
    void nativeApplied(std::uint16_t member,const EFLinkNative_Proof& proof) {
        std::lock_guard lock(mutex);require(api->native_applied(context,member,&proof));
    }
    std::optional<Message> poll() {
        // Hold the same lock over the size query, allocation and consumption;
        // another peer cannot replace the front message between these calls.
        std::lock_guard lock(mutex);Message result;
        const auto query=api->poll(context,&result.head,nullptr,0);
        if(query==EFLinkNative_Empty)return {};
        if(query!=EFLinkNative_BufferTooSmall||result.head.size>1024*1024)throw std::runtime_error("invalid native kernel message");
        result.body.resize((std::max)(std::uint32_t(1),result.head.size));
        require(api->poll(context,&result.head,result.body.data(),static_cast<std::uint32_t>(result.body.size())));
        result.body.resize(result.head.size);return result;
    }
};
}
