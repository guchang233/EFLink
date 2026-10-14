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
public:
    void stop() noexcept {deadline=0;}
    void timed(std::uint64_t now,std::uint64_t duration) {
        if(duration<1000||duration>600000||now>=waiting-duration)throw std::runtime_error("trace duration outside limits");
        deadline=now+duration;
    }
    void singleAttack() noexcept {deadline=waiting;}
    bool awaitingAttack() const noexcept {return deadline.load()==waiting;}
    bool active(std::uint64_t now) const noexcept {const auto until=deadline.load();return until&&now<until;}
    bool collecting(std::uint64_t now) const noexcept {const auto until=deadline.load();return until&&until!=waiting&&now<until;}
    void nativeAttack(std::uint64_t now) noexcept {
        auto expected=waiting;
        deadline.compare_exchange_strong(expected,now<waiting-3000?now+3000:0);
    }
};
}
