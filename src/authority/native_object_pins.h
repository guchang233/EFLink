#pragma once
// Owns managed roots, not game state. Last-reference release queues work; only
// the owning game thread calls the native GC API. No root handle is serialized.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace eflink::worldauthority {
class NativeObjectPins;
class NativeObjectPin {
    friend class NativeObjectPins;
    const void* registry=nullptr;
    std::uintptr_t address=0;
    std::uint64_t handle=0;
    bool uncertain=false;
    NativeObjectPin* next=nullptr;
};
using NativePin=std::shared_ptr<const NativeObjectPin>;
class NativeObjectPins {
    struct State {
        std::thread::id owner;
        const void* provider=nullptr;
        std::function<std::uint64_t(std::uintptr_t)> retain;
        std::function<void(std::uint64_t)> release;
        std::atomic<std::size_t> live=0;
        std::atomic<bool> uncertain=false;
        std::mutex mutex;
        NativeObjectPin* pending=nullptr;
        bool sweeping=false,healthy=true;
        std::atomic<std::uint64_t> failures=0;
        void queue(NativeObjectPin* pin) noexcept {
            // Intrusive nodes avoid an allocation in the shared_ptr deleter.
            try {std::lock_guard lock(mutex);pin->next=pending;pending=pin;}
            catch(...) {} // Forced process teardown must not call a dead runtime.
        }
        ~State() {
            // Normal module shutdown must have drained all handles already.
            // Do not call game code from a late/foreign-thread finalizer.
            while(pending) {auto* pin=pending;pending=pin->next;delete pin;}
        }
    };
    std::shared_ptr<State> state=std::make_shared<State>();
    bool onThread() const {return state->owner!=std::thread::id{}&&state->owner==std::this_thread::get_id();}
public:
    NativeObjectPins()=default;
    NativeObjectPins(const NativeObjectPins&)=delete;
    NativeObjectPins& operator=(const NativeObjectPins&)=delete;
    ~NativeObjectPins() {if(onThread())sweep();}
    void bind(const void* identity,std::function<std::uint64_t(std::uintptr_t)> retain,std::function<void(std::uint64_t)> release) {
        if(!identity||!retain||!release)throw std::runtime_error("native root provider unavailable");
        if(state->owner!=std::thread::id{}&&!onThread())throw std::runtime_error("native roots belong to another game thread");
        if(state->provider==identity) {
            if(!onThread())throw std::runtime_error("native roots belong to another game thread");return;
        }
        if(state->live||state->sweeping||state->uncertain)throw std::runtime_error("native roots must drain before replacing their runtime");
        state->owner=std::this_thread::get_id();state->provider=identity;
        state->retain=std::move(retain);state->release=std::move(release);state->healthy=true;
    }
    NativePin retain(std::uintptr_t address) {
        if(!address||!onThread()||!state->provider||!state->healthy||state->sweeping)
            throw std::runtime_error("native root allocation is unavailable on this thread");
        auto* raw=new NativeObjectPin;
        std::shared_ptr<NativeObjectPin> result(raw,[value=state](NativeObjectPin* pin) noexcept {value->queue(pin);});
        raw->registry=state.get();raw->address=address;
        try {raw->handle=state->retain(address);}
        catch(...) {state->uncertain=true;state->healthy=false;++state->failures;throw;}
        if(!raw->handle)throw std::runtime_error("native managed root allocation failed");
        ++state->live;return result;
    }
    bool matches(const NativePin& pin,std::uintptr_t address) const {
        return usable()&&pin&&pin->registry==state.get()&&pin->handle&&pin->address==address;
    }
    bool usable() const {return onThread()&&state->provider&&state->healthy&&!state->sweeping;}
    bool sweep() noexcept {
        if(state->owner==std::thread::id{})return !state->live;
        if(!onThread()||state->sweeping)return false;
        state->sweeping=true;NativeObjectPin* list=nullptr;
        try {std::lock_guard lock(state->mutex);list=state->pending;state->pending=nullptr;}
        catch(...) {state->sweeping=false;return false;}
        bool ok=true;
        while(list) {
            auto* pin=list;list=pin->next;pin->next=nullptr;
            if(pin->uncertain) {ok=false;state->queue(pin);continue;}
            try {
                if(pin->handle) {state->release(pin->handle);--state->live;}
                delete pin;
            }catch(...) {
                // An exception may follow the actual free. Retrying the same
                // numeric handle could free a newly reused slot in the runtime.
                pin->uncertain=true;state->uncertain=true;++state->failures;ok=false;state->queue(pin);
            }
        }
        state->healthy=ok&&!state->uncertain;state->sweeping=false;return state->healthy;
    }
    std::size_t live() const {return state->live.load();}
    std::uint64_t failures() const {return state->failures.load();}
    bool releaseUncertain() const {return state->uncertain.load();}
    bool safeToUnload() const {return !state->live&&!state->uncertain;}
};
}
