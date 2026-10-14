#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace eflink::worldauthority {
// A bounded diagnostic capture can wait for a human without recording while
// idle. Only a validated native attack starts the short collection window.
class NativeTraceWindow {
    static constexpr auto waiting=std::numeric_limits<std::uint64_t>::max();
    std::atomic<std::uint64_t> deadline{0};
    std::atomic<std::uint64_t> collectionDuration{3000};
public:
    void stop() noexcept {deadline=0;}
    void timed(std::uint64_t now,std::uint64_t duration) {
        if(duration<1000||duration>600000||now>=waiting-duration)throw std::runtime_error("trace duration outside limits");
        deadline=now+duration;
    }
    void singleAttack(std::uint64_t duration=3000) {
        if(duration<1000||duration>30000)throw std::runtime_error("single capture duration outside limits");
        collectionDuration=duration;deadline=waiting;
    }
    bool awaitingAttack() const noexcept {return deadline.load()==waiting;}
    bool active(std::uint64_t now) const noexcept {const auto until=deadline.load();return until&&now<until;}
    bool collecting(std::uint64_t now) const noexcept {const auto until=deadline.load();return until&&until!=waiting&&now<until;}
    void nativeAttack(std::uint64_t now) noexcept {
        auto expected=waiting;
        const auto duration=collectionDuration.load();
        deadline.compare_exchange_strong(expected,now<waiting-duration?now+duration:0);
    }
};
}
