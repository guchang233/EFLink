#pragma once
// Provenance survives the asynchronous native withdraw/finish pipeline. Only
// battle-instance-zero operations from IDs owned or retired by this adapter are
// withheld; a restored official monster with a real battle identity is allowed.
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

namespace eflink::authority {
class NativeOpPolicy {
    std::set<std::uint64_t> fed,retired;
    std::size_t limit;
    void reserve(const std::vector<std::uint64_t>& ids) const {
        std::set<std::uint64_t> extra;
        for(auto id:ids)if(id&&!fed.contains(id)&&!retired.contains(id))extra.insert(id);
        if(extra.empty())return;
        std::size_t used=fed.size();for(auto id:retired)if(!fed.contains(id))++used;
        if(extra.size()>limit-used)throw std::runtime_error("native operation provenance limit reached");
    }
public:
    explicit NativeOpPolicy(std::size_t capacity=8192):limit(capacity){}
    void rememberFed(std::uint64_t id) {reserve({id});if(id)fed.insert(id);}
    void rememberRetired(const std::vector<std::uint64_t>& ids) {
        // Reserve the whole batch first: never partially withdraw objects after
        // losing the ability to attribute their later teardown operations.
        reserve(ids);for(auto id:ids)if(id)retired.insert(id);
    }
    bool suppress(std::uint64_t serverId,std::uint64_t battleInstance) const {
        if((serverId>>56)==0xEF)return true; // current room-owned actor IDs
        return serverId&&battleInstance==0&&(fed.contains(serverId)||retired.contains(serverId));
    }
    std::size_t fedCount() const{return fed.size();}
    std::size_t retiredCount() const{return retired.size();}
};
}
