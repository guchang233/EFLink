#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include "BetterEndfield/ThirdPartyModule.h"
#include "nlohmann/json.hpp"

namespace eflink {
// EF Link's interface: one transparent overlay window laid over the game's client
// area, rendered with its own D3D11 device through DirectComposition. It draws the
// HUD (name tags of every player, the room roster with positions) and, on the menu
// key, becomes interactive and shows the menu. Nothing is drawn through the game's
// renderer. Requests run on the game thread (tickGame) and never block drawing.
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
    // Game thread, every frame: executes the interface's queued requests.
    void tickGame(const BE_HostApiV1* host);
    void shutdownGame(const BE_HostApiV1* host);
    std::uint64_t frames() const {return overlayFrames.load();}
    nlohmann::json diagnostics() const;
    struct Call {nlohmann::json input;std::promise<nlohmann::json> result;};
    // Queue a request for the game thread; the future is fulfilled there.
    std::future<nlohmann::json> post(const nlohmann::json& input);
private:
    void runOverlay(nlohmann::json options);
    void runPreview(Request request, nlohmann::json options);
    Request request;
    std::jthread thread;
    std::mutex callsMutex;std::deque<std::shared_ptr<Call>> calls;
    std::atomic<bool> stopping=false,openRequested=false,menuVisible=false,uiReady=false,overlayReady=false,gameThreadSeen=false;
    std::atomic<std::uint64_t> overlayFrames=0,callsServed=0;
    mutable std::mutex errorMutex;std::string lastError;
    void setError(std::string error) {std::lock_guard lock(errorMutex);lastError=std::move(error);}
};
}
