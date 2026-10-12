#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <memory>
#include "BetterEndfield/ThirdPartyModule.h"
#include "nlohmann/json.hpp"

namespace eflink {
// Window class of the standalone preview renderer. One process hosts a single
// preview, so the class name carries the process id. Shared with the preview
// launcher, which waits for it before falling back to its own message loop.
inline std::wstring previewWindowClass(unsigned long process) {return L"EFLink.Preview."+std::to_wstring(process);}
// Runtime ImGui is rendered on Unity's Canvas callback, with no overlay HWND.
// A separate Win32 renderer is available only to the standalone preview tool.
class Gui {
public:
    using Request=std::function<nlohmann::json(const nlohmann::json&)>;
    Gui();
    ~Gui();
    void start(Request request, const nlohmann::json& options);
    void stop();
    void open() {openRequested=true;}
    bool visible() const {return menuVisible.load();}
    bool ready() const {return uiReady.load();}
    void tickGame(const BE_HostApiV1* host);
    void shutdownGame(const BE_HostApiV1* host);
    std::uint64_t frames() const {return nativeFrames.load();}
    nlohmann::json diagnostics() const;
private:
    struct NativeState;
    std::unique_ptr<NativeState> native;
    void run(Request request, nlohmann::json options);
    std::jthread thread;
    std::atomic<bool> stopping=false,openRequested=false,menuVisible=false,uiReady=false;
    std::atomic<std::uint64_t> nativeFrames=0;
};
}
