#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace eflink {
// Native travel consumes this choice; no network packet or snapshot can grant it.
class HostMapConsent {
    std::string destination;
    std::uint64_t serial=0;
    bool waiting=false,declined=false,notice=false;
public:
    void reset() {destination.clear();waiting=declined=notice=false;}
    void observe(std::string next,bool alreadyThere) {
        if(destination.empty()) {destination=std::move(next);return;} // Initial join.
        if(next!=destination) {
            destination=std::move(next);++serial;
            waiting=!alreadyThere;declined=false;notice=waiting;
        }else if(alreadyThere)waiting=declined=notice=false;
    }
    bool pending() const {return waiting;}
    bool deferred() const {return declined;}
    std::uint64_t token() const {return serial;}
    bool takeNotice() {const bool out=notice;notice=false;return out;}
    void choose(std::uint64_t token,bool follow) {
        if(!waiting||token!=serial)throw std::runtime_error("房主的地图已变化，请重新选择");
        waiting=!follow;declined=!follow;notice=false;
    }
    void explicitTeleport(const std::string& target) {
        if(target==destination)waiting=declined=notice=false;
    }
};
}
