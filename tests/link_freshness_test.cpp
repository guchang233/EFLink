#include "link_freshness.h"
#include <iostream>
#include <cstdlib>
void check(bool value,const char* text) {if(!value){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}std::cout<<"PASS "<<text<<'\n';}
int main() {
    using F=eflink::AuthorityFreshness;using S=F::State;
    F f;const std::uint64_t t=100000;
    check(f.state(t)==S::Unseen&&!f.open(t)&&!f.live(t)&&!f.current(t),"nothing seen keeps every gate closed");
    f.update(t);check(f.state(t)==S::Desynced,"an increment without any baseline cannot open the room");
    f.baseline(t);check(f.open(t)&&f.live(t)&&f.current(t),"a complete baseline opens immediately");
    check(f.open(t+F::kStallMs),"the stall threshold itself is still normal");
    const auto stall=t+F::kStallMs+1;
    check(f.state(stall)==S::Stalled&&!f.open(stall)&&f.live(stall)&&!f.current(stall),
        "short stall freezes new operations but keeps the applied picture");
    const auto lost=t+F::kDesyncMs+1;
    check(f.state(lost)==S::Desynced&&!f.live(lost),"long gap is a desync");
    f.baseline(t);
    // A stall gap followed by updates: needs the full streak before reopening.
    auto at=t+F::kStallMs+500;f.update(at);
    check(f.state(at)==S::Stalled,"first update after a stall is not enough to reopen");
    for(int i=1;i<F::kRecoverStreak-1;++i) {at+=50;f.update(at);check(f.state(at)==S::Stalled,"recovery streak still building");}
    at+=50;f.update(at);check(f.open(at),"consecutive aligned updates reopen after a stall");
    // A desync gap: increments never reopen; only a baseline does.
    at+=F::kDesyncMs+500;f.update(at);
    for(int i=0;i<10;++i) {at+=50;f.update(at);}
    check(f.state(at)==S::Desynced,"after a desync, heartbeats and deltas alone never reopen");
    f.baseline(at);check(f.open(at),"one complete baseline repairs a desync");
    // A stall interrupted by another stall restarts the streak.
    at+=F::kStallMs+100;f.update(at);at+=50;f.update(at);
    at+=F::kStallMs+100;f.update(at);at+=50;f.update(at);
    check(f.state(at)==S::Stalled,"a second gap restarts the recovery streak");
    f.reset();check(f.state(at)==S::Unseen,"reset forgets everything");
    return 0;
}
