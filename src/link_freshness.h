#pragma once
// One freshness state for the authority channel. Every gate in the session reads
// this instead of comparing its own millisecond age.
//
//   Normal    updates are current: shared operations may open.
//   Stalled   a short gap (or still recovering from one): new intents and grants
//             are frozen, already applied objects and the picture are kept.
//   Desynced  a real gap: the caller withdraws room operations and requires a
//             complete baseline; nothing short of one reopens the guest.
//
// Leaving Stalled needs kRecoverStreak consecutive accepted updates, or one
// complete baseline, so a flapping link does not toggle operations every frame.
// The witness staging window in world_authority.h is a different parameter.
#include <cstdint>

namespace eflink {
class AuthorityFreshness {
public:
    enum class State {Unseen,Normal,Stalled,Desynced};
    static constexpr std::uint64_t kStallMs=2000,kDesyncMs=4000;
    static constexpr int kRecoverStreak=3;

    void reset() {*this=AuthorityFreshness{};}
    // An accepted incremental update (guest: aligned heartbeat or delta).
    void update(std::uint64_t now) {
        const std::uint64_t gap=lastSeen?now-lastSeen:UINT64_MAX;
        if(gap>kDesyncMs)baselineRequired=true;
        if(gap>kStallMs) {recovering=true;streak=0;}
        lastSeen=now;
        if(recovering&&++streak>=kRecoverStreak) {recovering=false;streak=0;}
    }
    // A complete state (guest: applied full baseline; host: its own native observation).
    void baseline(std::uint64_t now) {lastSeen=now;baselineRequired=recovering=false;streak=0;}
    State state(std::uint64_t now) const {
        if(!lastSeen)return State::Unseen;
        const auto age=now-lastSeen;
        if(baselineRequired||age>kDesyncMs)return State::Desynced;
        if(recovering||age>kStallMs)return State::Stalled;
        return State::Normal;
    }
    // Shared operations (intents, grants, acting) may proceed.
    bool open(std::uint64_t now) const {return state(now)==State::Normal;}
    // Applied objects stay in place (Normal or Stalled).
    bool live(std::uint64_t now) const {const auto s=state(now);return s==State::Normal||s==State::Stalled;}
    // The data itself is recent: the host may publish it (recovery streak aside).
    bool current(std::uint64_t now) const {return lastSeen&&!baselineRequired&&now-lastSeen<=kStallMs;}
    std::uint64_t age(std::uint64_t now) const {return lastSeen?now-lastSeen:0;}
    static const char* name(State s) {
        switch(s) {case State::Unseen:return "unseen";case State::Normal:return "normal";case State::Stalled:return "stalled";default:return "desynced";}
    }
private:
    std::uint64_t lastSeen=0;
    bool baselineRequired=false,recovering=false;
    int streak=0;
};
}
